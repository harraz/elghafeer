# PIR Motion State Diagram

```mermaid
stateDiagram-v2
    [*] --> Ready: boot / PIR LOW

    Ready: PIR is LOW
    Ready: waiting for next rising edge
    Ready --> ThrottleCheck: PIR LOW -> HIGH

    state ThrottleCheck <<choice>>
    ThrottleCheck --> WaitingForLow: PIR_INTERVAL not elapsed
    ThrottleCheck --> MotionAccepted: PIR_INTERVAL elapsed

    MotionAccepted: publish motion JSON
    MotionAccepted: include relay and throttle fields
    MotionAccepted --> RelayDecision

    state RelayDecision <<choice>>
    RelayDecision --> StartRelay: SKIP_LOCAL_RELAY=false and relay OFF
    RelayDecision --> PublishOnly: SKIP_LOCAL_RELAY=true or relay already ON

    StartRelay: set relay HIGH
    StartRelay: start relay timeout
    PublishOnly: do not change relay state

    StartRelay --> WaitingForLow
    PublishOnly --> WaitingForLow

    WaitingForLow: PIR remains HIGH
    WaitingForLow: no additional motion event
    WaitingForLow --> Ready: PIR HIGH -> LOW

    Ready --> Ready: PIR LOW
    WaitingForLow --> WaitingForLow: PIR HIGH
```

Relay timeout runs independently: when `RELAY_MAX_ON_DURATION` elapses, the relay is set LOW and `Relay_OFF (timer expired)` is published. That does not create a new motion event.
