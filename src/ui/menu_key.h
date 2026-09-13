#pragma once
/* Opening the menu must not depend on how the game took the keyboard: it reads through
   DirectInput or GetKeyboardState rather than the message queue, and DirectInput can stop key
   messages reaching the window at all. So the key is watched two ways -- the window's own key
   messages, and GetAsyncKeyState -- and both report the same thing: whether the key is down.
   Only menu_key_press turns that into a press.

   It used to be the other way round, with each route announcing a press of its own. When both
   saw the same press they announced it twice, and because the two announcements did not always
   land in the same frame the two toggles often cancelled: the menu stopped responding to the
   key for as long as that timing held, which is what "F11 does nothing any more" was. Levels
   cannot disagree about how many presses happened, so that failure is now impossible by
   construction rather than by careful bookkeeping. */
struct menu_key {
    int level;      /* the key's state as of the last poll */
    int msg_down;   /* down according to the window's key messages */
    int msg_tapped; /* a whole press arrived by message since the last poll */
    int stale;      /* consecutive polls where msg_down outlived the hardware */
};

/* A message-route "down" that outlives the hardware for this many consecutive polls is a
   KEYUP that never arrived, not a key being held: a real press spans far more polls than
   this at any tick rate. The point is that the level can always fall again -- a level stuck
   high would look exactly like the key having stopped working. */
#define MENU_KEY_STALE_POLLS 30

static int menu_key_press(struct menu_key* k, int polled_down, int focus) {
    if (!focus)                              { k->msg_down = 0; k->msg_tapped = 0; k->stale = 0; }
    else if (k->msg_down && !polled_down)    { if (++k->stale >= MENU_KEY_STALE_POLLS) { k->msg_down = 0; k->stale = 0; } }
    else                                     { k->stale = 0; }

    int down  = focus && (k->msg_down || polled_down);
    int press = down && !k->level;
    /* A press that both began and ended between two polls never shows up as a level at all.
       That is not a human tapping the key, but a long frame -- the log calls them gaps -- can
       stretch the window between polls far enough for a normal press to fall inside it.
       Only when the key is down neither now nor before: otherwise the message is describing
       the press this poll has already counted from the level rising. */
    if (!down && !k->level && k->msg_tapped && focus) press = 1;
    k->msg_tapped = 0;
    k->level = down;
    return press;
}

