# Event Model

`EngineEvent` types: Ack, Fill, CancelAck, Reject.

The matching thread is the sole producer of the SPSC ring. A logger or gateway
thread is the sole consumer via `poll_events`.
