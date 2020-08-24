/*
 * B-Em Joystick Mapping
 *
 * Steve Fosdick 2018
 */

#include "b-em.h"
#include "config.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include <ctype.h>

typedef struct {
    int   js_adc_chan;
    float js_scale;
    int   js_nkey;
    int   js_pkey;
} js_axis_map_t;

typedef struct {
    int num_axes;
    js_axis_map_t *axes_map;
} js_stick_t;

typedef struct {
    int   js_button;
    int   js_key;
} js_btn_map_t;

typedef struct {
    const char *js_name;
    ALLEGRO_JOYSTICK *js_id;
    int num_stick;
    js_stick_t *js_sticks;
    int num_butn;
    js_btn_map_t *js_btns;
} joystick_map_t;

static joystick_map_t *joystick_map;
static joystick_map_t *joystick_end;
static int num_joystick;

joymap_t *joymaps;
int joymap_count;
int joymap_num;

static void *js_malloc(size_t size)
{
    void *ptr;

    if ((ptr = malloc(size)))
        return ptr;
    log_fatal("joystick: out of memory");
    exit(1);
}

static void alloc_joystick(joystick_map_t *jsptr, ALLEGRO_JOYSTICK *js)
{
    int num_stick, stick_num, num_axes, num_butn;
    js_stick_t *stick;
    js_axis_map_t *axis;

    jsptr->num_stick = num_stick = al_get_joystick_num_sticks(js);
    jsptr->js_sticks = stick = js_malloc(jsptr->num_stick * sizeof(js_axis_map_t));
    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        stick->num_axes = num_axes = al_get_joystick_num_axes(js, stick_num);
        stick->axes_map = axis = js_malloc(num_axes * sizeof(js_axis_map_t));
        stick++;
    }
    jsptr->num_butn = num_butn = al_get_joystick_num_buttons(js);
    jsptr->js_btns = js_malloc(jsptr->num_butn * sizeof(js_btn_map_t));
}

static int default_chan(int stick, int axis)
{
    switch(stick) {
        case 0:
            switch(axis) {
                case 0: return 1;
                case 1: return 2;
            }
            break;
        case 1:
            switch(axis) {
                case 0: return 3;
                case 1: return 4;
            }
            break;
    }
    return 0;
}

static void reset_joystick(joystick_map_t *jsptr, int js_num)
{
    int stick_num, axis_num, butn_num;
    js_stick_t *stick;
    js_axis_map_t *axis;
    js_btn_map_t *butn;

    stick = jsptr->js_sticks;
    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        axis = stick->axes_map;
        for (axis_num = 0; axis_num < stick->num_axes; axis_num++) {
            if (num_joystick == 1)
                axis->js_adc_chan = default_chan(js_num, axis_num);
            else
                axis->js_adc_chan = default_chan(stick_num, axis_num);
            axis->js_scale = 1.0;
            axis->js_nkey = 0;
            axis->js_pkey = 0;
            axis++;
        }
        stick++;
    }
    butn = jsptr->js_btns;
    for (butn_num = 0; butn_num < jsptr->num_butn; butn_num++) {
        if (num_joystick == 1)
            butn->js_button = butn_num & 1;
        else if (js_num == 0)
            butn->js_button = 1;
        else if (js_num == 1)
            butn->js_button = 2;
        else
            butn->js_button = 0;
        butn->js_key = 0;
        butn++;
    }
}

static void init_joysticks(void)
{
    int js_num;
    joystick_map_t *jsptr;
    ALLEGRO_JOYSTICK *js;

    jsptr = joystick_map = js_malloc(num_joystick * sizeof(joystick_map_t));
    for (js_num = 0; js_num < num_joystick; js_num++) {
        if ((js = al_get_joystick(js_num))) {
            jsptr->js_id = js;
            jsptr->js_name = al_get_joystick_name(js);
            log_debug("joystick: found joystick '%s'", jsptr->js_name);
            alloc_joystick(jsptr, js);
            reset_joystick(jsptr, js_num);
        }
        else {
            jsptr->js_name   = NULL;
            jsptr->js_id     = NULL;
            jsptr->js_sticks = NULL;
            jsptr->js_btns   = NULL;
        }
        jsptr++;
    }
    joystick_end = jsptr;
}

static int cmp_joymap(const void *va, const void *vb)
{
    const joymap_t *ja = va;
    const joymap_t *jb = vb;
    return strcmp(ja->name, jb->name);
}

static void read_joymaps(void)
{
    const char *sect, *name;
    ALLEGRO_CONFIG_SECTION *siter;
    int num_jm, jm_num;

    if (bem_cfg) {
        num_jm = 0;
        for (sect = al_get_first_config_section(bem_cfg, &siter); sect; sect = al_get_next_config_section(&siter))
            if (strncasecmp(sect, "joymap", 6) == 0)
                num_jm++;
        if (num_jm == 0)
            log_debug("joystick: no joymaps found");
        else {
            joymap_count = num_jm;
            joymaps = js_malloc(num_jm * sizeof(joymap_t));
            jm_num = 0;
            for (sect = al_get_first_config_section(bem_cfg, &siter); sect; sect = al_get_next_config_section(&siter)) {
                if (strncasecmp(sect, "joymap", 6) == 0) {
                    joymaps[jm_num].sect = sect;
                    for (name = sect + 6; isspace(*name); name++)
                        ;
                    joymaps[jm_num++].name = name;
                    log_debug("joystick: found joymap %s", name);
                }
            }
            qsort(joymaps, num_jm, sizeof(joymap_t), cmp_joymap);
        }
    }
}

static const char rebuke[] = "joystick: invalid %s %s at section %s, key %s";

static void apply_axes(const char *sect, joystick_map_t *jsptr)
{
    int stick_num, axis_num;
    js_stick_t *stick;
    js_axis_map_t *axis;
    const char *value;
    int adc_chan, key_num;
    float scale;
    char key[50];

    stick = jsptr->js_sticks;
    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        axis = stick->axes_map;
        for (axis_num = 0; axis_num < stick->num_axes; axis_num++) {
            snprintf(key, sizeof key, "stick%daxis%dadc", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, sect, key))) {
                if ((adc_chan = atoi(value)) >= 0 && adc_chan <= 4) {
                    axis->js_adc_chan = adc_chan;
                    if (adc_chan)
                        log_debug("joystick: mapped stick %d axis %d to BBC ADC channel %d", stick_num, axis_num, adc_chan);
                    else
                        log_debug("joystick: mapped stick %d axis %d to 'not action'", stick_num, axis_num);
                }
                else
                    log_warn(rebuke, "BBC ADC channel", value, sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dscale", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, sect, key))) {
                if ((scale = atof(value)) != 0.0) {
                    axis->js_scale = scale;
                    log_debug("joystick: scaling for stick %d axis %d set to %g", stick_num, axis_num, scale);
                }
                else
                    log_warn(rebuke, "scale", value, sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dnkey", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, sect, key))) {
                if ((key_num = keydef_lookup_name(value))) {
                    axis->js_nkey = key_num;
                    log_debug("joysticK: mapped stick %d axis %d negative direction to key %d", stick_num, axis_num, key_num);
                }
                else
                    log_warn(rebuke, "negative direction key", value, sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dpkey", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, sect, key))) {
                if ((key_num = keydef_lookup_name(value))) {
                    axis->js_pkey = key_num;
                    log_debug("joysticK: mapped stick %d axis %d positive negative direction to key %d", stick_num, axis_num, key_num);
                }
                else
                    log_warn(rebuke, "positive direction key", value, sect, key);
            }
            axis++;
        }
        stick++;
    }
}

static void apply_buttons(const char *sect, joystick_map_t *jsptr)
{
    int butn_num;
    js_btn_map_t *butn;
    const char *value;
    int bbcbno, key_num;
    char key[50];

    butn = jsptr->js_btns;
    for (butn_num = 0; butn_num < jsptr->num_butn; butn_num++) {
        snprintf(key, sizeof key, "button%dbtn", butn_num);
        if ((value = al_get_config_value(bem_cfg, sect, key))) {
            if ((bbcbno = atoi(value)) >= 1 && bbcbno <= 2)
                butn->js_button = bbcbno;
            else
                log_warn(rebuke, "button", value, sect, key);
        }
        snprintf(key, sizeof key, "button%dkey", butn_num);
        if ((value = al_get_config_value(bem_cfg, sect, key))) {
            if ((key_num = keydef_lookup_name(value)))
                butn->js_key = key_num;
            else
                log_warn(rebuke, "key", value, sect, key);
        }
        butn++;
    }
}

void joystick_change_joymap(int mapno)
{
    joystick_map_t *js;
    const char *sect, *name;
    int js_num;

    if (mapno < joymap_count) {
        log_debug("joystick: changing to joymap #%d, %s", mapno, joymaps[mapno].name);
        sect = joymaps[mapno].sect;
        js = joystick_map;
        if ((name = al_get_config_value(bem_cfg, sect, "joystick"))) {
            while (!js->js_name || strcasecmp(name, js->js_name)) {
                if (++js == joystick_end) {
                    js = joystick_map;
                    if (js->js_name)
                        log_warn("joystick: joystick '%s' not found, applying map to first joystick '%s'", name, js->js_name);
                    break;
                }
            }
        }
        if (js->js_name) {
            js_num = js - joystick_map;
            reset_joystick(js, js_num);
            apply_axes(sect, js);
            apply_buttons(sect, js);
            joymap_num = mapno;
        }
    }
    else
        log_warn("joystick: attempt to change to invalid joymap no #%d", mapno);
}

void joystick_init(ALLEGRO_EVENT_QUEUE *queue)
{
    if (al_install_joystick()) {
        if ((num_joystick = al_get_num_joysticks()) > 0) {
            init_joysticks();
            read_joymaps();
            joystick_change_joymap(get_config_int("joystick", "joymap", 0));
            al_register_event_source(queue, al_get_joystick_event_source());
        }
        else
            log_debug("joystick: no joysticks found");
    }
    else
        log_warn("joystick: unable to install joystick driver");
}

void joystick_axis(ALLEGRO_EVENT *event)
{
    joystick_map_t *js;
    js_stick_t *stick;
    js_axis_map_t *axis;
    double value;

    log_debug("joystick: js_id=%p, stick=%d, axis=%d, pos=%g", event->joystick.id, event->joystick.stick, event->joystick.axis, event->joystick.pos);
    for (js = joystick_map; js < joystick_end; js++) {
        if (js->js_id == event->joystick.id) {
            if (event->joystick.stick < js->num_stick) {
                stick = js->js_sticks + event->joystick.stick;
                if (event->joystick.axis < stick->num_axes) {
                    axis = stick->axes_map + event->joystick.axis;
                    value = axis->js_scale * event->joystick.pos;
                    if (value < -1.0)
                        value = -1.0;
                    else if (value > 1.0)
                        value = 1.0;
                    if (axis->js_adc_chan)
                        joyaxes[axis->js_adc_chan-1] = value;
                    else
                        log_debug("joystick: unmapped axis %d", event->joystick.axis);
                    if (axis->js_nkey) {
                        if (value < -0.5)
                            key_down(axis->js_nkey);
                        else
                            key_up(axis->js_nkey);
                    }
                    if (axis->js_pkey) {
                        if (value > 0.5)
                            key_down(axis->js_pkey);
                        else
                            key_up(axis->js_pkey);
                    }
                }
                else
                    log_debug("joystick: axis num %d out of range", event->joystick.axis);
            }
            else
                log_debug("joystick: stick num %d out of range", event->joystick.stick);
            break;
        }
    }
}

static void joystick_button(ALLEGRO_EVENT *event, bool value, void (*key_func)(int keycode))
{
    joystick_map_t *js;
    js_btn_map_t *btn;

    log_debug("joystick: js_id=%p, button#%d down", event->joystick.id, event->joystick.button);
    for (js = joystick_map; js < joystick_end; js++) {
        if (js->js_id == event->joystick.id) {
            if ((btn = js->js_btns)) {
                if (event->joystick.button < js->num_butn) {
                    btn += event->joystick.button;
                    if (btn->js_button)
                        joybutton[btn->js_button-1] = value;
                    if (btn->js_key)
                        key_func(btn->js_key);
                }
            }
        }
    }
}

void joystick_button_down(ALLEGRO_EVENT *event)
{
    joystick_button(event, true, key_down);
}

void joystick_button_up(ALLEGRO_EVENT *event)
{
    joystick_button(event, false, key_up);
}
