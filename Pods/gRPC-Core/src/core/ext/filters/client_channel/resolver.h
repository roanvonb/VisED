/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/gprpp/abstract.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/iomgr/iomgr.h"

extern grpc_core::DebugOnlyTraceFlag grpc_trace_resolver_refcount;

namespace grpc_core {

/// Interface for name resolution.
///
/// This interface is designed to support both push-based and pull-based
/// mechanisms.  A push-based mechanism is one where the resolver will
/// subscribe to updates for a given name, and the name service will
/// proactively send new data to the resolver whenever the data associated
/// with the name changes.  A pull-based mechanism is one where the resolver
/// needs to query the name service again to get updated information (e.g.,
/// DNS).
///
/// Note: All methods with a "Locked" suffix must be called from the
/// combiner passed to the constructor.
class Resolver : public InternallyRefCountedWithTracing<Resolver> {
 public:
  // Not copyable nor movable.
  Resolver(const Resolver&) = delete;
  Resolver& operator=(const Resolver&) = delete;

  /// Requests a callback when a new result becomes available.
  /// When the new result is available, sets \a *result to the new result
  /// and schedules \a on_complete for execution.
  /// If resolution is fatally broken, sets \a *result to nullptr and
  /// schedules \a on_complete with an error.
  ///
  /// Note that the client channel will almost always have a request
  /// to \a NextLocked() pending.  When it gets the callback, it will
  /// process the new result and then immediately make another call to
  /// \a NextLocked().  This allows push-based resolvers to provide new
  /// data as soon as it becomes available.
  virtual void NextLocked(grpc_channel_args** result,
                          grpc_closure* on_complete) GRPC_ABSTRACT;

  /// Asks the resolver to obtain an updated resolver result, if
  /// applicable.
  ///
  /// This is useful for pull-based implementations to decide when to
  /// re-resolve.  However, the implementation is not required to
  /// re-resolve immediately upon receiving this call; it may instead
  /// elect to delay based on some configured minimum time between
  /// queries, to avoid hammering the name service with queries.
  ///
  /// For push-based implementations, this may be a no-op.
  ///
  /// If this causes new data to become available, then the currently
  /// pending call to \a NextLocked() will return the new result.
  ///
  /// Note: Currently, all resolvers are required to return a new result
  /// shortly after this method is called.  For pull-based mechanisms, if
  /// the implementation decides to delay querying the name service, it
  /// should immediately return a new copy of the previously returned
  /// result (and it can then return the updated data later, when it
  /// actually does query the name service).  For push-based mechanisms,
  /// the implementation should immediately return a new copy of the
  /// last-seen result.
  /// TODO(roth): Remove this requirement once we fix pick_first to not
  /// throw away unselected subchannels.
  virtual void RequestReresolutionLocked() GRPC_ABSTRACT;

  void Orphan() override {
    // Invoke ShutdownAndUnrefLocked() inside of the combiner.
    GRPC_CLOSURE_SCHED(
        GRPC_CLOSURE_CREATE(&Resolver::ShutdownAndUnrefLocked, this,
                            grpc_combiner_scheduler(combiner_)),
        GRPC_ERROR_NONE);
  }

  GRPC_ABSTRACT_BASE_CLASS

 protected:
  /// Does NOT take ownership of the reference to \a combiner.
  // TODO(roth): Once we have a C++-like interface for combiners, this
  // API should change to take a RefCountedPtr<>, so that we always take
  // ownership of a new ref.
  explicit Resolver(grpc_combiner* combiner);

  virtual ~Resolver();

  /// Shuts down the resolver.  If there is a pending call to
  /// NextLocked(), the callback will be scheduled with an error.
  virtual void ShutdownLocked() GRPC_ABSTRACT;

  grpc_combiner* combiner() const { return combiner_; }

 private:
  static void ShutdownAndUnrefLocked(void* arg, grpc_error* ignored) {
    Resolver* resolver = static_cast<Resolver*>(arg);
    resolver->ShutdownLocked();
    resolver->Unref();
  }

  grpc_combiner* combiner_;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_H */
