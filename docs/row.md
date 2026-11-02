# Menu row text

A module that runs from a menu row is called with a context describing the row.
Writing text is two calls:

```
z5_row_set_text(element, buffer, length_in_bytes)
z5_row_draw(element, r1, r2, r3)
```

The text is UTF-16, so `length_in_bytes` is twice the number of characters, and
the buffer must be large enough for twice the character count.

The row keeps the pointer, therefore the buffer must stay valid after the call
returns. Allocating a fresh buffer on every redraw and never freeing it leaks;
reusing one buffer for the lifetime of the module is both leak-free and safe.
