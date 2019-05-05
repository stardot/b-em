#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "tapecat-allegro.h"
#include "csw.h"
#include "uef.h"

static ALLEGRO_TEXTLOG *textlog;
ALLEGRO_EVENT_SOURCE uevsrc;

void cataddname(char *s)
{
    if (textlog)
        al_append_native_text_log(textlog, "%s\n", s);
}

static void *tapecat_thread(ALLEGRO_THREAD *thread, void *tdata)
{
    ALLEGRO_TEXTLOG *ltxtlog = (ALLEGRO_TEXTLOG *)tdata;
    ALLEGRO_EVENT_QUEUE *queue;
    ALLEGRO_EVENT event;

    if ((queue = al_create_event_queue())) {
        al_init_user_event_source(&uevsrc);
        al_register_event_source(queue, &uevsrc);
        al_register_event_source(queue, al_get_native_text_log_event_source(ltxtlog));
        do
            al_wait_for_event(queue, &event);
        while (event.type != ALLEGRO_EVENT_NATIVE_DIALOG_CLOSE);
        textlog = NULL;                    // set the global that cataddname will see to NULL
        al_close_native_text_log(ltxtlog); // before destorying the textlog with our local copy.
        al_destroy_event_queue(queue);
    }
    return NULL;
}

static void start_cat(void)
{
    if (csw_ena)
        csw_findfilenames();
    else
        uef_findfilenames();
}

void gui_tapecat_start(void)
{
    ALLEGRO_TEXTLOG *ltxtlog;
    ALLEGRO_THREAD *thread;

    if (textlog)
        start_cat();
    else if ((ltxtlog = al_open_native_text_log("B-Em Tape Catalogue", ALLEGRO_TEXTLOG_MONOSPACE))) {
        if ((thread = al_create_thread(tapecat_thread, ltxtlog))) {
            al_start_thread(thread);
            textlog = ltxtlog; // open to writes from cataddname.
            start_cat();
            return;
        }
        al_close_native_text_log(textlog);
        textlog = NULL;
    }
}

void gui_tapecat_close(void)
{
    ALLEGRO_EVENT event;

    if (textlog) {
        event.type = ALLEGRO_EVENT_NATIVE_DIALOG_CLOSE;
        al_emit_user_event(&uevsrc, &event, NULL);
    }
}
