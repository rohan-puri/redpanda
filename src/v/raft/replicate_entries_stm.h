#pragma once

#include "raft/consensus.h"
#include "utils/intrusive_list_helpers.h"

namespace raft {

/// A single-shot class. Utility method with state
/// Use with a lw_shared_ptr like so:
/// auto ptr = make_lw_shared<replicate_entries_stm>(..);
/// return ptr->apply()
///            .then([ptr]{
///                 // wait in background.
///                (void)ptr->wait().finally([ptr]{});
///            });
class replicate_entries_stm {
public:
    struct retry_meta {
        retry_meta(model::node_id node, int32_t ret)
          : retries_left(ret)
          , node(node) {}
        int32_t retries_left;
        model::node_id node;
        std::optional<append_entries_reply> value;
        safe_intrusive_list_hook hook;
        bool is_success() const { return value && value->success; }
        bool finished() const { return retries_left <= 0 || bool(value); }
    };

    using meta_ptr = std::unique_ptr<retry_meta>;
    replicate_entries_stm(
      consensus*, int32_t max_retries, append_entries_request);
    ~replicate_entries_stm();

    /// assumes that this is operating under the consensus::_op_sem lock
    /// returns after majority have responded
    future<> apply();

    /// waits for the remaining background futures
    future<> wait();

private:
    future<std::vector<append_entries_request>> share_request_n(size_t n);
    future<> dispatch_one(retry_meta&);
    future<append_entries_reply>
      do_dispatch_one(model::node_id, append_entries_request);
    future<> process_replies();
    std::pair<int32_t, int32_t> partition_count() const;

    consensus* _ptr;
    /// we keep a copy around until we finish the retries
    append_entries_request _req;
    // list to all nodes & retries per node
    semaphore _sem;
    counted_intrusive_list<retry_meta, &retry_meta::hook> _ongoing;
    std::vector<meta_ptr> _replies;
    seastar::gate _req_bg;
};
} // namespace raft