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
    ALLEGRO_JOYSTICK *js_id;
    int num_stick;
    js_stick_t *js_sticks;
    int num_butn;
    js_btn_map_t *js_btns;
} joystick_map_t;

static joystick_map_t *joystick_map;
static joystick_map_t *joystick_end;
joymap_t *joymaps;

int joymap_count;
int joymap_index[2] = {0, 0};

int joystick_count;
int joystick_index[2] = {-1, -1};
const char ** joystick_names;


static void *js_malloc(size_t size)
{
    void *ptr;

    if ((ptr = malloc(size)))
        return ptr;
    log_fatal("joystick: out of memory");
    exit(1);
}

static const char hori_fighting_stick_allegro_name[64] = {-26, -91, -109, -26, -107, -92, -26, -91, -105, -26, -111, -82, -25, -119, -91, -28, -100, -96, -26, -75, -95, -30, -127, -91, -26, -123, -112, -30, -127, -92, -25, -119, -112, -30, -127, -81, -27, -115, -107, -30, -127, -126, -26, -107, -74, -25, -115, -78, -26, -67, -87, -30, -127, -82, -30, -72, -79, 48, -31, -74, -87, -25, -104, 0};
static const char hori_fighting_stick_display_name[20] = "Hori fighting stick";

static void clear_joystick_map(int joystick)
{
    joystick_map_t *jsptr = &joystick_map[joystick_index[joystick]];
    js_stick_t *stick = jsptr->js_sticks;
    js_axis_map_t *axis;
    js_btn_map_t *butn = jsptr->js_btns;
    int stick_num, axis_num, butn_num;
    if (-1 == joystick_index[joystick]) return;

    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        axis = stick->axes_map;
        for (axis_num = 0; axis_num < stick->num_axes; axis_num++) {
            axis->js_scale = 1;
            axis->js_adc_chan = 0;
            axis->js_nkey = 0;
            axis->js_pkey = 0;
            axis++;
        }
        stick++;
    }

    for (butn_num = 0; butn_num < jsptr->num_butn; butn_num++) {
        butn->js_button = 0;
        butn->js_key    = 0;
        butn++;
    }
}

static const char rebuke[] = "joystick: invalid %s %s at section %s, key %s";

static void apply_axes_map(int joystick)
{
    int stick_num, axis_num;
    joystick_map_t *jsptr = &joystick_map[joystick_index[joystick]];
    joymap_t *joymap = &joymaps[joymap_index[joystick]];
    js_stick_t *stick = jsptr->js_sticks;
    js_axis_map_t *axis;
    const char *value;
    float scale;
    char key[50];
    int adc_chan, key_num;

    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        axis = stick->axes_map;
        for (axis_num = 0; axis_num < stick->num_axes; axis_num++) {
            snprintf(key, sizeof key, "stick%daxis%dadc", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, joymap->sect, key))) {
                if ((adc_chan = atoi(value)) >= 0 && adc_chan <= 4) {
                    axis->js_adc_chan = adc_chan;
                    if (adc_chan)
                        log_debug("joystick: mapped stick %d axis %d to BBC ADC channel %d", stick_num, axis_num, adc_chan);
                    else
                        log_debug("joystick: mapped stick %d axis %d to 'not action'", stick_num, axis_num);
                }
                else
                    log_warn(rebuke, "BBC ADC channel", value, joymap->sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dscale", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, joymap->sect, key))) {
                if ((scale = atof(value)) != 0.0) {
                    axis->js_scale = scale;
                    log_debug("joystick: scaling for stick %d axis %d set to %g", stick_num, axis_num, scale);
                }
                else
                    log_warn(rebuke, "scale", value, joymap->sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dnkey", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, joymap->sect, key))) {
                if ((key_num = keydef_lookup_name(value))) {
                    axis->js_nkey = key_num;
                    log_debug("joysticK: mapped stick %d axis %d negative direction to key %d", stick_num, axis_num, key_num);
                }
                else
                    log_warn(rebuke, "negative direction key", value, joymap->sect, key);
            }
            snprintf(key, sizeof key, "stick%daxis%dpkey", stick_num, axis_num);
            if ((value = al_get_config_value(bem_cfg, joymap->sect, key))) {
                if ((key_num = keydef_lookup_name(value))) {
                    axis->js_pkey = key_num;
                    log_debug("joysticK: mapped stick %d axis %d positive negative direction to key %d", stick_num, axis_num, key_num);
                }
                else
                    log_warn(rebuke, "positive direction key", value, joymap->sect, key);
            }
            axis++;
        }
        stick++;
    }
}

static void apply_button_map(int joystick)
{
    joystick_map_t *jsptr = &joystick_map[joystick_index[joystick]];
    joymap_t *joymap = &joymaps[joymap_index[joystick]];
    int butn_num, key_num, btn_mask = 1 + 2 * tricky_sega_adapter;
    js_btn_map_t *butn = jsptr->js_btns;
    const char *value;
    char key[50];
    
    for (butn_num = 0; butn_num < jsptr->num_butn; butn_num++) {
        snprintf(key, sizeof key, "button%dbtn", butn_num);
        if ((value = al_get_config_value(bem_cfg, joymap->sect, key)))
            butn->js_button = (atoi(value) & btn_mask) + 1;
        snprintf(key, sizeof key, "button%dkey", butn_num);
        if ((value = al_get_config_value(bem_cfg, joymap->sect, key))) {
            if ((key_num = keydef_lookup_name(value)))
                butn->js_key = key_num;
            else
                log_warn(rebuke, "key", value, joymap->sect, key);
        }
        butn++;
    }
}

void remap_joystick(int joystick)
{
    if (-1 == joystick_index[joystick]) return;
    clear_joystick_map(joystick);
    apply_axes_map(joystick);
    apply_button_map(joystick);
}

static void alloc_joystick(joystick_map_t *jsptr, ALLEGRO_JOYSTICK *js)
{
    int stick_num;
    js_stick_t *stick;
    jsptr->num_stick = al_get_joystick_num_sticks(js);
    jsptr->js_sticks = stick = js_malloc(jsptr->num_stick * sizeof(js_axis_map_t));
    for (stick_num = 0; stick_num < jsptr->num_stick; stick_num++) {
        stick->num_axes = al_get_joystick_num_axes(js, stick_num);
        stick->axes_map = js_malloc(stick->num_axes * sizeof(js_axis_map_t));
        stick++;
    }
    jsptr->num_butn = al_get_joystick_num_buttons(js);
    jsptr->js_btns = js_malloc(jsptr->num_butn * sizeof(js_btn_map_t));
}

static void free_joysticks()
{
    while (joystick_map != joystick_end--)
    {
        int j;
        free(joystick_end->js_btns);
        for (j = 0; j < joystick_end->num_stick; ++j)
            free(joystick_end->js_sticks[j].axes_map);
        free(joystick_end->js_sticks);
    }
    free(joystick_map);
    joystick_map = joystick_end = NULL;
}

static void init_joysticks(void)
{
    int js_num, js_used = 0;
    joystick_map_t *jsptr;
    ALLEGRO_JOYSTICK *js;

    jsptr = joystick_map = js_malloc(joystick_count * sizeof(joystick_map_t));
    joystick_names = js_malloc(joystick_count * sizeof(joystick_names[0]));
    for (js_num = 0; js_num < joystick_count; js_num++) {
        if ((js = al_get_joystick(js_num)) && al_get_joystick_active(js)) {
            jsptr->js_id = js;
            joystick_names[js_num] = al_get_joystick_name(js);
            if (!strncmp(joystick_names[js_num], hori_fighting_stick_allegro_name, 63)) // name seems t be meaningless!
                joystick_names[js_num] = hori_fighting_stick_display_name;
            log_debug("joystick: found joystick '%s'", joystick_names[js_num]);
            alloc_joystick(jsptr, js);
            if (js_used < 2) joystick_index[js_used++] = js_num;
        }
        else {
            joystick_names[js_num] = NULL;
            jsptr->js_id           = NULL;
            jsptr->js_sticks       = NULL;
            jsptr->js_btns         = NULL;
            jsptr->num_butn = jsptr->num_stick = 0;
        }
        jsptr++;
    }
    joystick_end = jsptr;
    change_joystick(0, joystick_index[0]);
    change_joystick(1, joystick_index[1]);
}

//static int cmp_joymap(const void *va, const void *vb)
//{
//    const joymap_t *ja = va;
//    const joymap_t *jb = vb;
//    return strcmp(ja->name, jb->name);
//}

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
//            qsort(joymaps, num_jm, sizeof(joymap_t), cmp_joymap);
        }
    }
}

void change_joystick(int joystick, int index)
{
    int i;
    char name[] = {'P', 'l', 'a', 'y', 'e', 'r', ' ', '1' + joystick, '\0'};
    if (-1 != joystick_index[joystick] && -1 == index) // removing joystick
        clear_joystick_map(joystick);
    joystick_index[joystick] = index;
    if (-1 == joystick_index[joystick]) {
        if (-1 != joystick_index[joystick ^ 1])
            change_joystick(joystick ^ 1, joystick_index[joystick ^ 1]);
        return;
    }

    joymap_index[joystick] = (joystick_index[joystick ^ 1] >= 0) ? 1 + joystick : 0;
    if (joymap_index[joystick] > joymap_count) // expect first three joymaps to be single player, player 1 aplayer 2 ("default" in provided .cfg)
        joymap_index[joystick] = joymap_count - 1;
    for (i = 0; i < joymap_count; ++i)
        if (strstr(joymaps[i].name, joystick_names[index])) { // partial match
            joymap_index[joystick] = i;
            break;
        }
    for (i = 0; i < joymap_count; ++i)
        if (!strcmp(joystick_names[index], joymaps[i].name)) { // exact match
            joymap_index[joystick] = i;
            break;
        }
    if (joystick_index[joystick ^ 1] >= 0) { // two players
        for (i = 0; i < joymap_count; ++i) {
            if (!strncmp(joystick_names[index], joymaps[i].name, strlen(joystick_names[index])) && strcmp(name, joymaps[i].name + strlen(joystick_names[index]) + 2)) { // exact match
                joymap_index[joystick] = i;
                break;
            }
        }
    }
    remap_joystick(joystick);
    remap_joystick(joystick ^ 1);
}

void joystick_init(ALLEGRO_EVENT_QUEUE *queue)
{
    if (al_install_joystick()) {
        if ((joystick_count = al_get_num_joysticks()) > 0) {
            read_joymaps();
            init_joysticks();
            al_register_event_source(queue, al_get_joystick_event_source());
        }
        else
            log_debug("joystick: no joysticks found");
    }
    else
        log_warn("joystick: unable to install joystick driver");
}

void joystick_rescan_sticks()
{
    ALLEGRO_JOYSTICK * ids[2] = {-1 == joystick_index[0] ? NULL : joystick_map[joystick_index[0]].js_id, -1 == joystick_index[1] ? NULL : joystick_map[joystick_index[1]].js_id};
    int j;
    if (!al_reconfigure_joysticks()) return;
    free_joysticks();
    init_joysticks();
    joystick_index[0] = joystick_index[1] = -1;
    for (j = 0; j < joystick_count; ++j)
    {
        if (joystick_map[j].js_id == ids[0])
            joystick_index[0] = j;
        if (joystick_map[j].js_id == ids[1])
            joystick_index[1] = j;
    }
    remap_joystick(0);
    remap_joystick(1);
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

static void joystick_button(ALLEGRO_EVENT *event, bool value, void (*key_func)(uint8_t bbckey))
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
