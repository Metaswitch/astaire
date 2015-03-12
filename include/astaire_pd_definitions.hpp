#ifndef ASTAIRE_PD_DEFINITIONS__
#define ASTAIRE_PD_DEFINITIONS__

#include <string>
#include "pdlog.h"

// The fields for each PDLog instance contains:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice,
//              and Info.  Directly corresponds to the syslog severity types.
//              Only PDLOG_ERROR or PDLOG_NOTICE are used.
//              See syslog_facade.h for definitions.
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition
//              if it is an error.

const static PDLog1<const char*> CL_ASTAIRE_INVALID_OPTION
(
  PDLogBase::CL_ASTAIRE_ID + 1,
  PDLOG_ERR,
  "Fatal - Unknown command line option %s.  Run with --help for options.",
  "There was an invalid command line option in /etc/clearwater/config",
  "The application will exit and restart until the problem is fixed.",
  "Correct the /etc/clearwater/config file."
);

const static PDLog CL_ASTAIRE_STARTED
(
  PDLogBase::CL_ASTAIRE_ID + 2,
  PDLOG_ERR,
  "Astaire started.",
  "The Astaire application is starting.",
  "Normal.",
  "None."
);

const static PDLog CL_ASTAIRE_ENDED
(
  PDLogBase::CL_ASTAIRE_ID + 3,
  PDLOG_ERR,
  "Astaire ended - Termination signal received - terminating.",
  "Astaire has been terminated by Monit or has exited.",
  "The Astaire service is not longer available.",
    "(1). This occurs normally when Astaire is stopped. "
    "(2). If Astaire hit an internal error then monit can restart Astaire."
);

const static PDLog1<const char*> CL_ASTAIRE_CRASHED
(
  PDLogBase::CL_ASTAIRE_ID + 4,
  PDLOG_ERR,
  "Fatal - Astaire has exited or crashed with signal %s.",
  "Astaire has encountered a fatal software error or has been terminated",
  "The application will exit and restart until the problem is fixed.",
  "Ensure that Astaire has been installed correctly and that it "
    "has valid configuration."
);

const static PDLog CL_ASTAIRE_RESYNC_FAILED
(
  PDLog::CL_ASTAIRE_ID + 5,
  PDLOG_ERR,
  "Astaire has failed to synchronise some data.",
  "Astaire was unable to reach all the previous replicas for some data.",
  "Not all data has been resynchronised, completing the scaling action now "
    "may result in loss of data or loss of redundancy",
  "Check the status of the memcached cluster and ensure network connectivity "
    "is possible between all nodes."
);

const static PDLog CL_ASTAIRE_START_RESYNC
(
  PDLog::CL_ASTAIRE_ID + 6,
  PDLOG_INFO,
  "Astaire has started a resync operation",
  "Astaire has detected an on-going cluster resize and is proactively "
    "resynchronising data between cluster members.",
  "Data is being resynced across the Memcached cluster.",
  "Wait until the current resync operation has completed before continuing "
    "with the cluster resize."
);

const static PDLog CL_ASTAIRE_COMPLETE_RESYNC
(
  PDLog::CL_ASTAIRE_ID + 7,
  PDLOG_INFO,
  "Astaire has completed a resync operation",
  "Astaire has synchronised all available data to the local node.",
  "The scale operation may be completed once all other Astaire instances have "
    "completed their resync operations.",
  "Once all other Astaire instances have completed their resync operations "
    "you may conrinue the cluster resize"
);
#endif
