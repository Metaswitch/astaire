/**
 * @file astaire_pd_definitions.hpp - Astaire problem determination logs
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
  "Fatal - An invalid command line option, %s, was passed to Astaire. The application will exit and restart until the problem is fixed. Run with --help for options.",
  "There was an invalid command line option in the configuration files.",
  "The application will exit and restart until the problem is fixed.",
  "Check that the configuration files in /etc/clearwater and /etc/clearwater/cluster_settings are correct."
);

const static PDLog CL_ASTAIRE_STARTED
(
  PDLogBase::CL_ASTAIRE_ID + 2,
  PDLOG_INFO,
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
    "(2). If Astaire fails to respond to monit queries in a timely manner, monit restarts the application."
);

const static PDLog1<const char*> CL_ASTAIRE_TERMINATED
(
  PDLogBase::CL_ASTAIRE_ID + 4,
  PDLOG_ERR,
  "Fatal - Astaire has exited or been terminated with signal %s.",
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
  "Not all data has been resynchronised. Completing a resize operation now "
    "may result in loss of data or loss of redundancy",
  "Check the status of the memcached cluster and ensure network connectivity "
    "is possible between all nodes."
);

const static PDLog CL_ASTAIRE_START_RESYNC
(
  PDLog::CL_ASTAIRE_ID + 6,
  PDLOG_INFO,
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
  PDLOG_INFO,
  "Astaire has completed a resync operation",
  "Astaire has synchronised all available data to the local node.",
  "If the cluster is being resized, this operation can be completed once all other "
    "Astaire instances have completed their resync operations.",
  "If the cluster is being resized, you may continue the resize operation once "
    "all other Astaire instances have completed their resync operations."
);
#endif
