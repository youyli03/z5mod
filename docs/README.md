# docs

Reference for behaviour and calls that were **verified on hardware**. Anything
still being investigated is not documented here; the top-level README carries
the current status table.

| Document | Contents |
| --- | --- |
| [abi.md](abi.md) | service calls, allocation, task creation, module lifecycle, code images |
| [loader.md](loader.md) | what the patched firmware does with a `.Z5M`: checks, call, free, row behaviour |
| [audio.md](audio.md) | the six-message audio protocol and the state machine built on it |
| [hooks.md](hooks.md) | notification callbacks, their codes, and what a callback may do |
| [row.md](row.md) | writing a text row in the menu |

Addresses appear where they are part of a protocol - the audio state word, the
callback slots. The patcher keeps its offsets in one platform table per firmware
version, and the SDK keeps its addresses in the headers.
