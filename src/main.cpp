#include "memcached_tap_client.hpp"
#include "memcachedstore.h"
#include "exception_handler.h"
#include "astaire.hpp"
#include "astaire_pd_definitions.hpp"
#include "astaire_statistics.h"

#include <getopt.h>
#include <boost/filesystem.hpp>
#include <logger.h>

struct options
{
  std::string local_memcached_server;
  std::string cluster_settings_file;
  bool log_to_file;
  std::string log_directory;
  int log_level;
  bool alarms_enabled;
};

enum Options
{
  LOCAL_NAME=256+1,
  CLUSTER_SETTINGS_FILE,
  LOG_FILE,
  LOG_LEVEL,
  ALARMS_ENABLED,
  HELP,
};

const static struct option long_opt[] =
{
  {"local-name",             required_argument, NULL, LOCAL_NAME},
  {"cluster-settings-file",  required_argument, NULL, CLUSTER_SETTINGS_FILE},
  {"log-file",               required_argument, NULL, LOG_FILE},
  {"log-level",              required_argument, NULL, LOG_LEVEL},
  {"alarms-enabled",         no_argument,       NULL, ALARMS_ENABLED},
  {"help",                   no_argument,       NULL, HELP},
  {NULL,                     0,                 NULL, 0},
};

static std::string options_description = "";

void usage(void)
{
  puts("Options:\n"
       "\n"
       " --local-name <hostname>    Specify the name of the local memcached server\n"
       " --cluster-settings-file=<filename>\n"
       "                            The filename of the cluster settings file\n"
       " --log-file=<directory>     Log to file in specified directory\n"
       " --log-level=N              Set log level to N (default: 4)\n"
       " --alarms-enabled           Enable SNMP alarms (default: disabled)\n"
       " --help                     Show this help screen\n"
       );
}

int init_logging_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case LOG_FILE:
      options.log_to_file = true;
      options.log_directory = std::string(optarg);
      break;

    case LOG_LEVEL:
      options.log_level = atoi(optarg);
      break;

    default:
      // Ignore other options at this point
      break;
    }
  }

  return 0;
}

int init_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;

  optind = 0;
  opterr = 0;
  while ((opt = getopt_long(argc, argv, options_description.c_str(), long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case LOCAL_NAME:
      options.local_memcached_server = optarg;
      break;

    case CLUSTER_SETTINGS_FILE:
      options.cluster_settings_file = optarg;
      break;

    case ALARMS_ENABLED:
      options.alarms_enabled = true;
      break;

    case HELP:
      usage();
      CL_ASTAIRE_ENDED.log();
      closelog();
      exit(0);

    case LOG_LEVEL:
    case LOG_FILE:
      // Handled already in init_logging_options
      break;

    default:
      CL_ASTAIRE_INVALID_OPTION.log(argv[optind - 1]);
      LOG_ERROR("Unknown option: %s.  Run with --help for options.\n",
                argv[optind - 1]);
      exit(2);
    }
  }

  return 0;
}

static sem_t term_sem;
ExceptionHandler* exception_handler;

// Signal handler that triggers astaire termination.
void terminate_handler(int /*sig*/)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void signal_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  // Log the signal, along with a backtrace.
  LOG_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  LOG_COMMIT();

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  CL_ASTAIRE_CRASHED.log(strsignal(sig));
  closelog();

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  struct options options;
  options.log_to_file = false;
  options.log_level = 0;
  options.log_directory = "";
  options.local_memcached_server = "";
  options.cluster_settings_file = "";

  boost::filesystem::path p = argv[0];
  openlog(p.filename().c_str(), PDLOG_PID, PDLOG_LOCAL6);
  CL_ASTAIRE_STARTED.log();

  if (init_logging_options(argc, argv, options) != 0)
  {
    closelog();
    return 1;
  }

  Log::setLoggingLevel(options.log_level);
  if (options.log_to_file && (options.log_directory != ""))
  {
    Log::setLogger(new Logger(options.log_directory, p.filename().string()));
  }

  LOG_STATUS("Log level set to %d", options.log_level);

  std::stringstream options_ss;
  for (int ii = 0; ii < argc; ii++)
  {
    options_ss << argv[ii];
    options_ss << " ";
  }
  std::string options_str = "Command-line options were: " + options_ss.str();
  LOG_INFO(options_str.c_str());

  if (init_options(argc, argv, options) != 0)
  {
    closelog();
    return 1;
  }

  if (options.local_memcached_server == "")
  {
    LOG_ERROR("Must supply local memcached server name");
    return 2;
  }

  if (options.cluster_settings_file == "")
  {
    LOG_ERROR("Must supply cluster settings file");
    return 2;
  }

  LOG_STATUS("Astaire starting up");

  Alarm* astaire_resync_alarm = NULL;
  if (options.alarms_enabled)
  {
    astaire_resync_alarm = new Alarm("astaire",
                                     AlarmDef::ASTAIRE_RESYNC_IN_PROGRESS,
                                     AlarmDef::MINOR);
    AlarmReqAgent::get_instance().start();
    AlarmState::clear_all("astaire");
  }

  // These values match those in MemcachedStore's constructor
  MemcachedStoreView* view = new MemcachedStoreView(128, 2);
  MemcachedConfigReader* view_cfg =
    new MemcachedConfigFileReader(options.cluster_settings_file);

  // Create statistics infrastructure.
  std::string stats[] = { "astaire_global", "astaire_connections" };
  LastValueCache* lvc = new LastValueCache(2, stats, p.filename().string());
  AstaireGlobalStatistics* global_stats = new AstaireGlobalStatistics(lvc);
  AstairePerConnectionStatistics* per_conn_stats = new AstairePerConnectionStatistics(lvc);

  Astaire* astaire = new Astaire(view,
                                 view_cfg,
                                 astaire_resync_alarm,
                                 global_stats,
                                 per_conn_stats,
                                 options.local_memcached_server);

  sem_wait(&term_sem);

  if (options.alarms_enabled)
  {
    AlarmReqAgent::get_instance().stop();
  }

  CL_ASTAIRE_ENDED.log();
  delete astaire;
  delete view_cfg;
  delete view;

  closelog();
  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);

  return 0;
}
