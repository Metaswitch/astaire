/**
 * @file main.cpp - Astaire entry point
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "memcached_tap_client.hpp"
#include "astaire.hpp"
#include "astaire_pd_definitions.hpp"
#include "astaire_statistics.hpp"
#include "logger.h"
#include "utils.h"
#include "astaire_alarmdefinition.h"
#include "proxy_server.hpp"
#include "communicationmonitor.h"

#include <sstream>
#include <getopt.h>
#include <boost/filesystem.hpp>

struct options
{
  std::string local_memcached_server;
  std::string cluster_settings_file;
  std::string bind_addr;
  bool log_to_file;
  std::string log_directory;
  int log_level;
  std::string pidfile;
  bool daemon;
};

enum Options
{
  LOCAL_NAME=256+1,
  CLUSTER_SETTINGS_FILE,
  BIND_ADDR,
  LOG_FILE,
  LOG_LEVEL,
  PIDFILE,
  DAEMON,
  HELP,
};

const static struct option long_opt[] =
{
  {"local-name",             required_argument, NULL, LOCAL_NAME},
  {"cluster-settings-file",  required_argument, NULL, CLUSTER_SETTINGS_FILE},
  {"bind-addr",              required_argument, NULL, BIND_ADDR},
  {"log-file",               required_argument, NULL, LOG_FILE},
  {"log-level",              required_argument, NULL, LOG_LEVEL},
  {"pidfile",                required_argument, NULL, PIDFILE},
  {"daemon",                 no_argument,       NULL, DAEMON},
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
       " --bind-addr=<IP>           The IP address to bind to (default: all)\n"
       " --log-file=<directory>     Log to file in specified directory\n"
       " --log-level=N              Set log level to N (default: 4)\n"
       " --pidfile=<filename>       Write pidfile\n"
       " --daemon                   Run as daemon\n"
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

    case BIND_ADDR:
      options.bind_addr = optarg;
      break;

    case PIDFILE:
      options.pidfile = std::string(optarg);
      break;

    case DAEMON:
      options.daemon = true;
      break;

    case HELP:
      usage();
      CL_ASTAIRE_ENDED.log();
      exit(0);

    case LOG_LEVEL:
    case LOG_FILE:
      // Handled already in init_logging_options
      break;

    default:
      CL_ASTAIRE_INVALID_OPTION.log(argv[optind - 1]);
      TRC_ERROR("Unknown option: %s.  Run with --help for options.\n",
                argv[optind - 1]);
      exit(2);
    }
  }

  return 0;
}

static sem_t term_sem;

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

  // Log the signal, along with a backtrace.
  TRC_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  TRC_COMMIT();

  CL_ASTAIRE_TERMINATED.log(strsignal(sig));

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.log_to_file = false;
  options.log_level = 0;
  options.log_directory = "";
  options.local_memcached_server = "";
  options.cluster_settings_file = "";
  options.bind_addr = "";
  options.pidfile = "";
  options.daemon = false;

  // Initialise ENT logging before making "Started" log
  PDLogStatic::init(argv[0]);

  CL_ASTAIRE_STARTED.log();

  if (init_logging_options(argc, argv, options) != 0)
  {
    return 1;
  }

  Log::setLoggingLevel(options.log_level);
  boost::filesystem::path p = argv[0];
  if (options.log_to_file && (options.log_directory != ""))
  {
    Log::setLogger(new Logger(options.log_directory, p.filename().string()));
  }

  TRC_STATUS("Log level set to %d", options.log_level);

  std::stringstream options_ss;
  for (int ii = 0; ii < argc; ii++)
  {
    options_ss << argv[ii];
    options_ss << " ";
  }
  std::string options_str = "Command-line options were: " + options_ss.str();
  TRC_INFO(options_str.c_str());

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  if (options.local_memcached_server == "")
  {
    TRC_ERROR("Must supply local memcached server name");
    return 2;
  }

  if (options.cluster_settings_file == "")
  {
    TRC_ERROR("Must supply cluster settings file");
    return 2;
  }

  TRC_STATUS("Astaire starting up");

  if (options.daemon)
  {
    int errnum = Utils::daemonize();
    if (errnum != 0)
    {
      TRC_ERROR("Failed to convert to daemon, %d (%s)", errnum, strerror(errnum));
      exit(0);
    }
  }

  if (options.pidfile != "")
  {
    int rc = Utils::lock_and_write_pidfile(options.pidfile);
    if (rc == -1)
    {
      // Failure to acquire pidfile lock
      TRC_ERROR("Could not write pidfile - exiting");
      return 2;
    }
  }

  start_signal_handlers();

  AlarmManager* alarm_manager = new AlarmManager();
  Alarm* astaire_resync_alarm = new Alarm(alarm_manager,
                                          "astaire",
                                          AlarmDef::ASTAIRE_RESYNC_IN_PROGRESS,
                                          AlarmDef::MINOR);

  // These values match those in MemcachedStore's constructor
  MemcachedStoreView* view = new MemcachedStoreView(128, 2);
  MemcachedConfigReader* view_cfg =
    new MemcachedConfigFileReader(options.cluster_settings_file);

  // Check that the cluster settings are valid. If they are not nothing is
  // going to work and it is better to restart and have monit alarm.
  MemcachedConfig dummy_cfg;
  if (!view_cfg->read_config(dummy_cfg))
  {
    TRC_ERROR("Cluster view config is invalid. Exiting");
    return 3;
  }

  // Create statistics infrastructure.
  std::string stats[] = { "astaire_global", "astaire_connections" };
  LastValueCache* lvc = new LastValueCache(2, stats, "astaire");
  AstaireGlobalStatistics* global_stats = new AstaireGlobalStatistics(lvc);
  AstairePerConnectionStatistics* per_conn_stats = new AstairePerConnectionStatistics(lvc);

  // Create communication monitor for memcached
  CommunicationMonitor* memcached_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                                    "astaire",
                                                                                    AlarmDef::ASTAIRE_MEMCACHED_COMM_ERROR,
                                                                                    AlarmDef::CRITICAL),
                                                                          "Astaire",
                                                                          "Memcached");
  // Create vbucket alarm
  Alarm* vbucket_alarm = new Alarm(alarm_manager,
                                   "astaire",
                                   AlarmDef::ASTAIRE_VBUCKET_ERROR,
                                   AlarmDef::MAJOR);

  MemcachedBackend* backend = new MemcachedBackend(view_cfg,
                                                   memcached_comm_monitor,
                                                   vbucket_alarm);

  // Start the memcached proxy server.
  ProxyServer* proxy_server = new ProxyServer(backend);
  
  if (!proxy_server->start(options.bind_addr.c_str()))
  {
    TRC_ERROR("Could not start proxy server, exiting");
    return 4;
  }

  // Start Astaire last as this might cause a resync to happen synchronously.
  Astaire* astaire = new Astaire(view,
                                 view_cfg,
                                 astaire_resync_alarm,
                                 global_stats,
                                 per_conn_stats,
                                 options.local_memcached_server);

  sem_wait(&term_sem);

  TRC_INFO("Astaire shutting down");
  CL_ASTAIRE_ENDED.log();
  delete proxy_server; proxy_server = NULL;
  delete memcached_comm_monitor; memcached_comm_monitor = NULL;
  delete vbucket_alarm; vbucket_alarm = NULL;
  delete backend; backend = NULL;
  delete per_conn_stats;
  delete global_stats;
  delete lvc;
  delete astaire;
  delete alarm_manager; alarm_manager = NULL;
  delete view_cfg;
  delete view;

  signal(SIGTERM, SIG_DFL);
  sem_destroy(&term_sem);

  return 0;
}
