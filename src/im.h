/* im.h - zwp_input_method_v2 for auto show/hide. */
#ifndef SLATEKBD_IM_H
#define SLATEKBD_IM_H

#include <stdbool.h>

struct app;

/* Get the input method object (if the manager exists and none is held). */
void im_acquire(struct app *a);
/* Destroy the input method object (auto off / exit). */
void im_release(struct app *a);
/* content-type purpose is numeric (digits, number, phone, pin)? */
bool im_purpose_numeric(unsigned purpose);

#endif
