#pragma once

// The distance on the co-op markers (the partner's icon, the marker wheel's): the waypoint draw's own text, over the icon (markers.c).
void markers_install(void);
void markers_poll(void); // control.c, every 250 ms: the QA scenarios' questions about the drawn markers (mw2coop_qa_markers_expect)
