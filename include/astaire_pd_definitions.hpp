/**
 * @file astaire_pd_definitions.hpp - Astaire problem determination logs
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef ASTAIRE_PD_DEFINITIONS__
#define ASTAIRE_PD_DEFINITIONS__

#include <string>
#include "pdlog.h"

// The fields for each PDLog instance contains:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice,
//              and Info. Only LOG_ERROR or LOG_NOTICE are used.
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition
//              if it is an error.

const static PDLog1<const char*> CL_ASTAIRE_INVALID_OPTION
(
  PDLogBase::CL_ASTAIRE_ID + 1,
  LOG_ERR,
  "Fatal - An invalid command line option, %s, was passed to Astaire. The application will exit and restart until the problem is fixed. Run with --help for options.",
  "There was an invalid command line option in the configuration files.",
  "The application will exit and restart until the problem is fixed.",
  "Check that the configuration files in /etc/clearwater and /etc/clearwater/cluster_settings are correct."
);

const static PDLog CL_ASTAIRE_STARTED
(
  PDLogBase::CL_ASTAIRE_ID + 2,
  LOG_INFO,
  "Astaire started.",
  "The Astaire application is starting.",
  "Normal.",
  "None."
);

const static PDLog CL_ASTAIRE_ENDED
(
  PDLogBase::CL_ASTAIRE_ID + 3,
  LOG_ERR,
  "Astaire ended - Termination signal received - terminating.",
  "Astaire has been terminated by Monit or has exited.",
  "The Astaire service is not longer available.",
    "(1). This occurs normally when Astaire is stopped. "
    "(2). If Astaire fails to respond to monit queries in a timely manner, monit restarts the application."
);

const static PDLog1<const char*> CL_ASTAIRE_TERMINATED
(
  PDLogBase::CL_ASTAIRE_ID + 4,
  LOG_ERR,
  "Fatal - Astaire has exited or been terminated with signal %s.",
  "Astaire has encountered a fatal software error or has been terminated",
  "The application will exit and restart until the problem is fixed.",
  "Ensure that Astaire has been installed correctly and that it "
    "has valid configuration."
);

const static PDLog CL_ASTAIRE_RESYNC_FAILED
(
  PDLog::CL_ASTAIRE_ID + 5,
  LOG_ERR,
  "Astaire has failed to synchronise some data.",
  "Astaire was unable to reach all the previous replicas for some data.",
  "Not all data has been resynchronised. Completing a resize operation now "
    "may result in loss of data or loss of redundancy",
  "Check the status of the memcached cluster and ensure network connectivity "
    "is possible between all nodes."
);

const static PDLog CL_ASTAIRE_START_RESYNC
(
  PDLog::CL_ASTAIRE_ID + 6,
  LOG_INFO,
  "Astaire has started a resync operation",
  "Astaire is proactively resynchronising data between cluster members."
    "This could be because:"
    "(1). Astaire has detected that the cluster is being resized."
    "(2). Astaire has detected the local Memcached process has been restarted."
    "(3). An operator has manually triggered a resync.",
  "Data is being resynced across the Memcached cluster.",
  "Wait until the current resync operation has completed before continuing "
    "with any cluster resize."
);

const static PDLog CL_ASTAIRE_COMPLETE_RESYNC
(
  PDLog::CL_ASTAIRE_ID + 7,
  LOG_INFO,
  "Astaire has completed a resync operation",
  "Astaire has synchronised all available data to the local node.",
  "If the cluster is being resized, this operation can be completed once all other "
    "Astaire instances have completed their resync operations.",
  "If the cluster is being resized, you may continue the resize operation once "
    "all other Astaire instances have completed their resync operations."
);
#endif
