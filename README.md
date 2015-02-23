# Astaire

## Active Resync for Memcached Clusters

Astaire pro-actively resynchronises data across a cluster of `Memcached` nodes, allowing for faster scale-up/scale-down.  Astaire works with the Project Clearwater `MemcachedStore` to create a dynamically scalable, geographically redundant, highly consistent transient data store.

Astaire is optional, the `MemcachedStore` implementation is capable of elastically scaling up/down without loss of data, but without Astaire, all the keys in the store have to be rewritten at least once before the resize can be called complete (and hence another resize can be started).

## How it works

`MemcachedStore` arranges the keys it is storing into a large number of "virtual buckets" (`vbuckets`) and allocates these `vbuckets` to available `Memcached` cluster members based on a deterministic algorithm (allowing each `MemcachedStore` instance to independently decide on the same allocation).  During a scaling operation, some of these `vbuckets` will be re-homed, either being moved onto the new servers or being moved off servers before they are terminated.  Without Astaire, `MemcachedStore` does these moves lazily, moving each key only when it is next written to the store.  This means that resizing the cluster takes as long as the longest lived key in the store (potentially unbounded).

Astaire uses `MemcachedStoreView` (a part of `MemcachedStore`) to calculate which `vbuckets` are being re-homed and then uses the newly added (in v1.6) `Memcached TAP protocol` to stream the affected keys off their old home and to inject them into their new home.  By taking advantage of `Memcached`'s built in consistency primitives and the work already done in `MemcachedStore` to deal with data-contention between clients in a large cluster, Astaire is able to stream the data into the correct new homes at close to line speed with no loss of data integrity.

If you want to run a large Clearwater deployment (or any large `MemcachedStore`-based cluster), we strongly recommend taking advantage of Astaire to allow quicker resizing operations, especially in orchestrated environments where long waits may cause wide-reaching slowdowns.

## Using Astaire

Astaire is very easy to use, and integrates into the standard resizing algorithm for a `MemcachedStore`-based cluster:

1. Update the `/etc/clearwater/cluster_settings` file to contain the `servers` and `new_servers` lines (reflecting the old and new member lists).
2. Reload the `MemcachedStore` (to pick up those changes).
3. Run `astaire <target host> /etc/clearwater/cluster_settings` on each node in the cluster.  Here `<target host>` is the entry in the `cluster_settings` file that corresponds to the local node.
4. Update `/etc/clearwater/cluster_settings` file to only list the new `servers` list.
5. Reload `MemcachedStore` to complete the resize.

## Project Clearwater

Astaire was originally written as part of [Project Clearwater](http://www.projectclearwater.org), an open-source IMS core, developed by [Metaswitch Networks](http://www.metaswitch.com/) and released under the [GNU GPLv3](http://www.projectclearwater.org/download/license/). You can find more information about it on [our website](http://www.projectclearwater.org/) or our [wiki](http://clearwater.readthedocs.org/en/latest/).
