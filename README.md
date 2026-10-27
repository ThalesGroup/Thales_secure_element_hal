# SE HAL Documentation
 
 
# Lock Mechanism
 
To allow Secure Element (SE) updates to be performed without interference from other applications, the SE HAL can be placed in a locked state. While locked, only the SE Update Agent is allowed to access the Secure Element.
 
Once the SE Update Agent has completed the update, the SE HAL is unlocked and becomes available again to all authorized applications.
 
The lock mechanism is controlled through the following Android system properties:
 
- `persist.vendor.sehal.lock`
  - Enables or disables the lock.
  - Value `1`: exclusive access is enforced.
  - Value `0`: the SE HAL is unlocked.
  - When enabled, access to the following operations is restricted:
    - `openBasicChannel`
    - `openLogicalChannel`
    - `transmit`
    - `closeChannel`
    - `reset`
 
- `persist.vendor.sehal.lock_duration`
  - Specifies the lock duration, in seconds, starting from SE HAL initialization.
  - Once the configured duration expires, the lock is automatically ignored, regardless of the value of `persist.vendor.sehal.lock`.
 
- `persist.vendor.sehal.lock_uid`
  - Specifies the UID of the SE Update Agent.
  - Calls originating from this UID bypass the lock restrictions.
 
When the SE HAL is locked, any caller whose UID does not match `persist.vendor.sehal.lock_uid` receives an `IOERROR` for the following operations:
 
- `openBasicChannel`
- `openLogicalChannel`
- `transmit`
- `closeChannel`
- `reset`
 
> **Note**
>
> The lock mechanism is enabled only if all three properties are present and contain valid integer values:
>
> - `persist.vendor.sehal.lock`
> - `persist.vendor.sehal.lock_duration`
> - `persist.vendor.sehal.lock_uid`
 
## Lock Initialization
 
During the first SE HAL initialization (typically during device boot), if the lock mechanism is enabled, the SE HAL is automatically locked for a minimum of 5 seconds. The effective lock duration is determined by the value of `persist.vendor.sehal.lock_duration`, but cannot be shorter than 5 seconds.
 
---
 
# Secure Element API
 
All Secure Element API methods are protected by a `LockGuard` to prevent concurrent access to the Secure Element and ensure thread-safe operation.
 
---
 
# Notification Callback
 
The notification callback is registered during the `init()` method under the following conditions:
 
- Only the OMAPI client is allowed to register a callback.
- If a callback is already registered, it is replaced by the new OMAPI callback.
- State notifications are sent only when the Secure Element state changes.
- Notifications are dispatched asynchronously on a dedicated thread, to prevent re-entrant calls (in case of callback method calls directly SE HAL).

---
 
# Client application lifecycle monitoring

- On each `init()` call, the service registers the client via `AIBinder_linkToDeath()`, associating it with a unique `DeathCookie` (callerId, PID, UID).
 - When a client terminates (crash or kill), the Binder triggers this callback. A dedicated thread is spawned to perform cleanup under the API lock, without blocking the Binder thread.
- if no other entry exists for that PID, then `all logical channels still open by that application are closed automatically`.
 - When SE HAL service is stopped, all remaining open channels are closed and registered callers are released.


