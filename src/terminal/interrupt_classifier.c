/* SPDX-License-Identifier: MIT */
#include "terminal/interrupt.h"

void interrupt_classifier_init(struct interrupt_classifier *classifier)
{
    classifier->state = INTERRUPT_CLASSIFIER_IDLE;
}

int interrupt_classifier_feed(struct interrupt_classifier *classifier, unsigned char byte)
{
    switch (classifier->state) {
    case INTERRUPT_CLASSIFIER_IDLE:
        if (byte == 0x1b)
            classifier->state = INTERRUPT_CLASSIFIER_ESCAPE_PENDING;
        return 0;
    case INTERRUPT_CLASSIFIER_ESCAPE_PENDING:
        if (byte == '[') {
            classifier->state = INTERRUPT_CLASSIFIER_CSI;
            return 0;
        }
        if (byte == 'O') {
            classifier->state = INTERRUPT_CLASSIFIER_SS3;
            return 0;
        }
        /* The previous Esc was bare; classify a second Esc as a new candidate. */
        classifier->state =
            byte == 0x1b ? INTERRUPT_CLASSIFIER_ESCAPE_PENDING : INTERRUPT_CLASSIFIER_IDLE;
        return 1;
    case INTERRUPT_CLASSIFIER_CSI:
        /* CSI final bytes are 0x40-0x7e; control and non-ASCII bytes invalidate the sequence. */
        if ((byte >= 0x40 && byte <= 0x7e) || byte < 0x20 || byte > 0x7e)
            classifier->state = INTERRUPT_CLASSIFIER_IDLE;
        return 0;
    case INTERRUPT_CLASSIFIER_SS3:
        classifier->state = INTERRUPT_CLASSIFIER_IDLE;
        return 0;
    }
    return 0;
}

int interrupt_classifier_timeout(struct interrupt_classifier *classifier)
{
    if (classifier->state != INTERRUPT_CLASSIFIER_ESCAPE_PENDING)
        return 0;
    classifier->state = INTERRUPT_CLASSIFIER_IDLE;
    return 1;
}
