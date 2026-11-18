#ifndef VMMOUSE_H
#define VMMOUSE_H

/* Call from ring 0 after enabling PS/2 AUX reporting and reading its ACK.
   Returns 1 if absolute mode is ready, 0 to keep using ordinary PS/2. */
int vmmouse_init(void);

/* Call after reading an AUX byte while absolute mode is active.
   Drain the queued absolute events. Return -1 if the device lost sync
   and was disabled, or the number of absolute events delivered. */
int vmmouse_poll(void (*on_event)(unsigned x, unsigned y, int left_down));

/* Map one 0..65535 axis to a zero-based text column or row. */
unsigned vmmouse_cell(unsigned value, unsigned cells);

#endif

