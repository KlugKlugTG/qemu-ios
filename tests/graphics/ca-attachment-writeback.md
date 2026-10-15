# Deferred CA surface ownership across attachment changes

A framebuffer can change color attachments while remaining bound. Previously,
the renderer marked its attached IOSurface dirty only on framebuffer binding,
`glFlush` or `glFinish`. The sequence below lost the first rendered surface:

1. Draw into tracked texture A on framebuffer F.
2. Replace F's color attachment with texture B (or the drawable renderbuffer).
3. Flush: the current attachment B is marked; A remains clean and is skipped by
   the deferred surface writeback.

The attachment handlers now mark A before replacing the color attachment. They
retain the existing frame-boundary flush policy, avoiding publication during
an unfinished CA composition. Depth/stencil attachment changes do not need this
color ownership transition. A failed mark returns before changing the attachment
or the framebuffer's drawable role. Desktop GL, native GLES and EAGL use the
same corrected handlers.

The offline regression includes both production handlers and the production
marking function. It verifies that A and B both survive the deferred flush
selection, and that no guest-storage copy occurs during the attachment switch.
The exact previous handlers fail the outgoing-A assertion. The storage copy in
this test is modeled; it is neither driver rendering nor guest acceptance.

Actual Linux and Android observations also show a full Home icon grid becoming
mostly black at a guest minute change, leaving Contacts, status and dock. Linux
raw guest pixels confirm this reaches the shared native publication path. The
lost-write sequence above is an established source defect; the existing logs do
not establish that it caused those particular minute-change frames. Qualification
requires a new exact engine, full native and frontend images before and after a
minute change, input-driven redraw, and clean shutdown. Preserve prior failing
captures and keep partial scenes out of full-scene acceptance.
