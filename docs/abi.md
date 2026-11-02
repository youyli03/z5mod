# Platform ABI

Everything below was verified on a camera unless marked otherwise.

## Service calls

Services are entered from **ARM state** with the function code in `ip`:

```
ip = 0x80<service><argc>00        ; e.g. 0x80340200 = snd_mbx, 2 arguments
svc #6
```

The firmware's own stubs wrap every call in this frame, and mirroring it is what
made our calls work:

```
stmfd sp!, {r4}
add   r4, sp, #4
stmfd sp!, {lr}
svc   #6
ldmfd sp!, {lr}
ldmfd sp!, {r4}
```

Call arguments go in `r0..r3`, the result comes back in `r0`.

**An `svc` is a function call.** Under AAPCS `r0..r3` and `ip` are caller-saved,
so inline assembly must declare them clobbered. Without that the compiler keeps
a live constant in `r2` across the call, the kernel overwrites it, and the next
iteration dereferences garbage. This was a hardware hang, twice.

## Allocation

```
z5_alloc(size, heap)   -> pointer, or 0        heap is 0 or 1
z5_free(ptr)           -> void                 the heap is chosen from the address
```

Two heaps exist; the second argument selects one. Allocation survives module
reloads, so it is the only place to keep state that must outlive one redraw.

## Tasks

Creating a task and starting it are two separate steps; only the second one
makes the handler run. A task object carries its own id, pump, priority, stack
size, name, mailbox and message size.

Two kinds of task are used:

* **pump task** - the handler runs inside the firmware's shared message pump.
  It must return promptly: blocking or looping inside it stalls every task that
  shares the pump.
* **raw task** - its own loop, entered once. It must yield (`rot_rdq`) or it
  occupies its core.

Messages are delivered by posting a node to a queue; the queue's handler
receives the node's payload.

## Module lifecycle

The module is re-read from the card and executed **on every menu redraw**, and
the image is freed as soon as the module returns. Three consequences:

1. module statics are reset on every redraw - state must live in the heap;
2. a pointer into the module image must never be handed to the firmware;
3. a heap pointer already handed to the firmware must not be freed.

## Code images

Code that has to run from `alloc()` memory (callbacks, task bodies) is copied
there as an image. An image is only valid when:

* it does not touch `r9` (the module's position-independent base);
* it does not branch outside its own byte range;
* its literal pool lies inside the range;
* it is entered with the Thumb bit set and is 4-byte aligned;
* the instruction cache is flushed after copying (`dsb`, `mcr p15,0,rX,c7,c5,0`,
  `dsb`, `isb`).
