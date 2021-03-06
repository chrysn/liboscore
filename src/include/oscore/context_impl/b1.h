#ifndef OSCORE_CONTEXT_B1_H
#define OSCORE_CONTEXT_B1_H

#include <oscore/context_impl/primitive.h>
#include <oscore_native/message.h>
#include <oscore/protection.h>

/** @file */

/** @ingroup oscore_contextpair
 *
 * @addtogroup oscore_context_b1 Security context with Appendix B.1 recovery
 *
 * @brief A pre-derived context implementation that occasionally needs to be persisted
 *
 * This security context contains a @ref oscore_context_primitive, along with
 * additional indicators for the mechanisms described in [Appendix B.1 of
 * RFC8613](https://tools.ietf.org/html/rfc8613#appendix-B.1).
 *
 * Such a security context is superior to a primitive context as it can be used
 * across reboots without the need to persist its state after any operation;
 * it can recover lost state by skipping sequence numbers or asking for
 * resubmission of the first request. A notable downside is that until the
 * replay window is recovered, replays of old messages can be used to slowly
 * exhaust own sequence numbers.
 *
 * There are aspects to its use:
 *
 * * Peristence: Two aspects of the context can be persisted, the sequence
 *   number and the replay window. Persisting the sequence number is mandatory,
 *   the replay window optional. (Persisting spares a round trip during the
 *   first request to the server from that given security context).
 *
 *   * Sequence number persistence
 *
 *     * When a context is created, the application needs to provide the last
 *       persisted sequence number in a @ref oscore_context_b1_initialize call.
 *
 *     * After that, and repeatedly later on, the application should query the
 *       next sequence number to persist using the
 *       @ref oscore_context_b1_get_wanted call.  When it has persisted that
 *       number, it uses @ref oscore_context_b1_allow_high call to inform the
 *       context
 *       that that sequence number has been persisted.
 *
 *       Failure to do this often or fast enough results in temporary errors
 *       when sending messages, but does not endanger security. (In particular,
 *       no own messages can be sent until @ref oscore_context_b1_allow_high
 *       has been called).
 *
 *       Once @ref oscore_context_b1_allow_high has been called, @ref
 *       oscore_context_b1_initialize must not be called in subsequent
 *       startups with any value of @ref oscore_context_b1_allow_high from
 *       earlier calls.  This is crucial for security; failure to do this
 *       correctly typically results in nonce reuse and subsequent breach of
 *       the key.
 *
 *       A method to extract and persist the current sequence number at
 *       shutdown (in analogy to the below) would be possible (mostly the
 *       documentation would become more verbose), but is currently not
 *       implemented as the ill-effect of not recovering a precise sequence
 *       number is just the loss of some sequence number space, and not an
 *       additional round-trip.
 *
 *   * Replay window persistence (optional)
 *
 *     An application can use the @ref oscore_context_b1_replay_extract
 *     function to extract the 9 byte necessary to express the replay window
 *     state. After that call, it must not use the security context any more --
 *     this is typically done at a controlled device shutdown, or when entering
 *     a deep sleep state in which the security context's data is lost.
 *
 *     It can then use that persisted replay window state once (!) at the next
 *     startup using @ref oscore_context_b1_initialize. The data must be
 *     removed (or marked as deleted) in the persistent storage before that
 *     function is called.  Failure to do so affects security with the same
 *     results as above.
 *
 *     On startups that were not immediately preceded by an extraction, no
 *     replay window is reinjected. That is fine, and only results in an
 *     additional roundtrip for the first exchange message.
 *
 * * Application integration: libOSCORE can not manage the additional exchanges
 *   for replay window recovery on its own, as that would include sending
 *   messages on its own. It does, however, assist the application author in
 *   sending the right messages:
 *
 *   * A server whose replay window was not initialized will see the first
 *     received message as @ref OSCORE_UNPROTECT_REQUEST_DUPLICATE. Rather than
 *     erring out with an unprotected 4.01 Unauthorized message, the server can
 *     use @ref oscore_context_b1_build_401echo to create a suitable response
 *     (which is a protected 4.01 with Echo option) if indicated by
 *     @ref oscore_context_b1_process_request.
 *
 *     Alternatively, it may build its own response (which may be a 4.01, or
 *     even an actual result in case of safe requests) and include the echo
 *     value reported by @ref oscore_context_b1_get_echo in it.
 *
 *     The call to @ref oscore_context_b1_process_request also serves to
 *     recognize any incoming Echo options and thus initialize the replay
 *     state.
 *
 *   * A client that receives a 4.01 response with an Echo option needs to
 *     resubmit the request, and use any Echo value found in the response in
 *     its next request.
 *
 *     Providing additional helpers here is [being considered](https://gitlab.com/oscore/liboscore/issues/47),
 *     and would profit from user feedback.
 *
 * @{
 */

/** @brief Data for a security context that can perform B.1 recovery
 *
 * This must always be initialized using @ref oscore_context_b1_initialize.
 * (It will stay practically unusable until @ref oscore_context_b1_allow_high
 * has been called as well, but until then the context is technically
 * initialized, it's just that most operations will fail).
 * */
struct oscore_context_b1 {
    /** @private
     *
     * @brief Underlying primitive context.
     *
     * Having this as an inlined first struct member means that contextpair.h
     * cases that access a primitive context directly or through a B.1 context
     * may have different code, but can be collapsed by the compiler as both
     * access a primitive context directly behind the data pointer.
     * */
    struct oscore_context_primitive primitive;
    /** @private
     *
     * @brief Upper limit to sequence numbers
     *
     * The security context will not deal out any sequence numbers equal or
     * above this value.
     */
    uint64_t high_sequence_number;
    /** @private
     *
     * @brief Echo value to send out and recognize
     *
     * This is initialized to the current sequence number when first used --
     * which is != 0 because it's first used when a response is formed, and if
     * it needs to be used then that response already pulled out a sequence
     * number.
     */
    uint8_t echo_value[PIV_BYTES];
    /** @private
     *
     * @brief Indicator of how many bytes of Echo value are populated
     *
     * If the echo_value has not been intialized, it is 0. (Given that the echo
     * value is a Partial IV, it never has zero length).
     */
    uint8_t echo_value_populated;
};

/** @brief Persistable replay data of a B.1 context
 *
 * Such a datum can be extracted at shutdown using @ref
 * oscore_context_b1_replay_extract and used in @ref
 * oscore_context_b1_initialize once. Between those, it can be persisted in
 * arbitrary form.
 * */
struct oscore_context_b1_replaydata {
    uint64_t left_edge;
    uint32_t window;
};

/** @brief Initialize a B.1 context
 *
 * This is the way to initialize a @ref oscore_context_b1 struct.
 *
 * @param[inout] secctx B.1 security context to initialize; must not be NULL,
 *     and must be partially initialized.
 * @param[in] immutables Primitive security context key material that will be
 *     used throughout the life time of the security context.
 * @param[in] seqno The last (and highest) value that was ever passed to a @ref
 *     oscore_context_b1_allow_high call to this context, or 0 for brand-new
 *     contexts.
 * @param[in] replaydata A struct previously obtained using
 *     @ref oscore_context_b1_replay_extract. Before this function is called,
 *     it must be ensuered that the same replaydata will not be passed in here
 *     again. Alternatively (ie. if replay extraction is not used, or if the
 *     extracted data has been removed before new one was extracted and
 *     persisted), NULL may be passed to start the Appendix B.1.2 recovery
 *     process.
 *
 */
void oscore_context_b1_initialize(
        struct oscore_context_b1 *secctx,
        const struct oscore_context_primitive_immutables *immutables,
        uint64_t seqno,
        const struct oscore_context_b1_replaydata *replaydata
        );

/** @brief State to a B.1 context that sequence numbers up to excluding @p
 * seqno may be used freely
 *
 * This must be called before using the security context, and may be called at
 * any later time with any value equal to or larger than the previous value
 * passed with the same function. A convenient way to come up with such values
 * that do not change too frequently is using
 * @ref oscore_context_b1_get_wanted.
 *
 * This must only be called when it can be guaranteed that later calls to @ref
 * oscore_context_b1_initialize will not give any value persisted earlier than
 * @p seqno.
 *
 * @param[inout] secctx B.1 security context to update
 * @param[in] seqno The persistent sequence number limit
 *
 */
OSCORE_NONNULL
void oscore_context_b1_allow_high(
        struct oscore_context_b1 *secctx,
        uint64_t seqno
        );

/** @brief The next sequence number a B.1 context wants to be allowed to use
 *
 * @param[in] secctx B.1 security context to query
 *
 * @return the sequence number that should be used on the next @ref
 * oscore_context_b1_allow_high call
 *
 * Note that this is a plain convenience function that implements static
 * increments of a default size, which are stepped whenever the previous
 * allocation is half used up. Applications are free to come up with their own
 * numbers based on predicted traffic, as long as the constraints of @ref
 * oscore_context_b1_allow_high are met.
 *
 */
OSCORE_NONNULL
uint64_t oscore_context_b1_get_wanted(
        struct oscore_context_b1 *secctx
        );

/** @brief Take the replay data of a security context for persistence
 *
 * @param[inout] secctx B.1 security context to shut down. This is marked inout
 *     as the security context is uninitialized after this.
 * @param[out] replaydata Location into which to move the replay window data.
 *
 * This function can be used during shutdown to take the security context's
 * replay window and make it available for the next startup.
 *
 * After calling this function, the security context must not be used any more;
 * instead, the same context can later be initialized using the extracted
 * replaydata in @ref oscore_context_b1_initialize.
 *
 */
OSCORE_NONNULL
void oscore_context_b1_replay_extract(
    struct oscore_context_b1 *secctx,
    struct oscore_context_b1_replaydata *replaydata
    );

/** @brief Find the Echo value used by a B.1 context for recovery
 *
 * This function provides access to the Echo value that is used (sent in
 * responses, and recognized in requests) when a server is trying to run B.1.2
 * replay window recovery.
 *
 * The obtained Echo value is valid until secctx is used again, and stable as
 * long as the context is only used (but not changed).
 *
 * It should only be called when the replay window is uninitialized, and
 * sequence numbers are available (as it takes one of its own to make the
 * implenetation easier); calling it under other circumstances has no lasting
 * side effects, but may result in the indication of a zero-length slice (which
 * does no harm security-wise as that value is not recognized later, worst case
 * it makes the first request fail) - but those preconditions are typically
 * satisfied when used.
 *
 * This must only be called on a B.1 backed security context.
 *
 */
OSCORE_NONNULL
void oscore_context_b1_get_echo(
        oscore_context_t *secctx,
        size_t *value_length,
        uint8_t **value
        );

/** @brief Helper function for processing incoming requests in B.1 contexts
 *
 * This function performs two tasks:
 *
 * * It checks whether it'd make sense to send an Echo value with the response
 *   to recover the replay window, returning the result.
 *
 * * It tries to recover the replay window using data from the incoming
 *   request. When it does, the request can be considered fresh in the sense of
 *   certainly not being a replay, and the request's unprotection status is
 *   upgraded from OSCORE_UNPROTECT_REQUEST_DUPLICATE to
 *   OSCORE_UNPROTECT_REQUEST_OK.
 *
 *   At the same time, the request ID's is-first-use flag is set.
 *
 * It is best run after unprotecting a request and before any further
 * processing. It is usually very cheap as it returns early on seeing the
 * security context's initialized state. If it returns true, a good next step
 * is building a response using @ref oscore_context_b1_build_401echo.
 *
 * @param[in] secctx The B.1 security context used with this request
 * @param[in] request A received request message
 * @param[inout] unprotectresult The result of the message's unprotect operation
 * @param[inout] requestid The request ID of he unprotected message
 * @return true if responding with an Echo option would help recover the replay window
 *
 */
OSCORE_NONNULL
bool oscore_context_b1_process_request(
        oscore_context_t *secctx,
        oscore_msg_protected_t *request,
        enum oscore_unprotect_request_result *unprotectresult,
        oscore_requestid_t *request_id
        );

/** @brief Build a 4.01 Unauthorized with Echo response
 *
 * This convenience function builds a protected 4.01 Unauthorized response with
 * a suitable Echo option into a native message that is sent in response to a
 * request that is rejected by the duplicate detection.
 *
 * It must only be used on security contexts backed by a B.1 context (or will
 * crash), and only if @ref oscore_context_b1_replay_is_uninitialized returned
 * true (or might send client and server into an endless exchange without
 * results).
 *
 * @param[inout] message Native message into which the response is written
 * @param[inout] secctx Security context to be used
 * @param[inout] requestid ID of the request that is responded to. This is
 *     formally inout as the protection process reserves the right to update
 *     the request ID, but is practically in only because the function is only
 *     called in situations when the request ID's first use flag is clear
 *     anyway.
 * @return true on success; the native message must be cleared by the
 *     application if it is to be sent, or must not be sent at all.
 */
OSCORE_NONNULL
bool oscore_context_b1_build_401echo(
        oscore_msg_native_t message,
        oscore_context_t *secctx,
        oscore_requestid_t *request_id
        );

/** @} */

#endif
