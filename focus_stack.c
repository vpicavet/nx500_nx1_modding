/*
Compile with:

arm-linux-gnueabihf-gcc -o example example.c `pkg-config --cflags --libs elementary` --sysroot=/path_to_arm_usr_and_lib/ -Wl,-dynamic-linker,/lib/ld-2.13.so

We need to specify the correct ld or it will not work on device.
*/
#include <Elementary.h>
#include <strings.h>
#include <sys/time.h>
#define SCREEN_WIDTH 540
#define MAX_STEPS 100
#define DEFAULT_STEPS 10

int debug = 1;

Evas_Object *win, *bg, *box, *btn_near, *btn_far, *btn_stack, *btn_sweep, *btn_plus, *btn_minus, *btn_quit, *entry_points, *table;
char stringline[255], label_entry[255], sample_text[255];
int focus_pos_near=1200, focus_pos_far=1100, focus_pos_min=0, focus_pos_max=0, number_points = DEFAULT_STEPS, shot_delay = 6;
int button_height = 45, button_width=150;

static void restore_touch()
{
	system("/usr/bin/st key touch click 360 240");	// TODO: quick hack to release input focus
}

static int get_af_position()
{
	FILE *fp;
	char *spl = NULL;
	fp = popen("/usr/bin/st cap iq af pos", "r");
	if (fp == NULL) {
		debug && printf("Failed get current focus position\n");
	} else {
		if (fgets(stringline, sizeof(stringline) - 1, fp) != NULL) {
			stringline[0] = '_';	// fix st output
			spl = strtok(stringline, " ");
			spl = strtok(NULL, " ");
			spl = strtok(NULL, " ");
		}
		pclose(fp);
		debug && printf("Current focus position: %s\n", spl);
		return atoi(spl);
	}
}

static void focus_to_position(int position)
{
	int amount = 0;
	amount = position - get_af_position();
	debug && printf("Focus to position at %d by %d\n", position, amount);
	sprintf(stringline, "/usr/bin/st cap iq af mv 255 %d 255", amount);
	debug && printf("CMD: %s\n", stringline);
	system(stringline);
}

static void focus_move(int amount)
{
	debug && printf("Focus move by %d\n", amount);
	sprintf(stringline, "/usr/bin/st cap iq af mv 255 %d 255", amount);
	debug && printf("CMD: %s\n", stringline);
	system(stringline);
}

static void run_stack(int near, int far, int steps, int delay) {
	int initial_position,current_position=0, step=0;
	double delta=0;
	evas_object_hide(win);
	system("/usr/bin/st app nx capture af-mode manual"); // show manual focus mode
	system("/usr/bin/st cap capdtm setusr AFMODE 0x70003"); // force manual focus mode
	sleep(2);
	focus_to_position(near);
	sleep(2);
	current_position = get_af_position();
	initial_position = current_position;
	delta = ((double)(far - current_position)) / (double)steps;
	debug && printf("delta: %f\n",delta);
	while (current_position>far && step < steps && step < MAX_STEPS) {
		step++;
		sleep(delay/2);
		system("st app nx capture single && st key click s1"); // capture single frame and exit photo preview is exists
		sleep(delay - delay/2);
		focus_move((int)(near + (int)(step*delta)-current_position));
		current_position = near + (int)(step*delta);
	}
	evas_object_show(win);
}

static void click_quit(void *data, Evas_Object * obj, void *event_info)
{
	elm_exit();
	exit(0);
}

static void click_near(void *data, Evas_Object * obj, void *event_info)
{
	focus_pos_near = get_af_position();
}

static void click_far(void *data, Evas_Object * obj, void *event_info)
{
	focus_pos_far = get_af_position();
}

static void click_stack(void *data, Evas_Object * obj, void *event_info)
{
	run_stack(focus_pos_near,focus_pos_far,number_points, shot_delay);
}

static void click_sweep(void *data, Evas_Object * obj, void *event_info)
{
}

static void click_plus(void *data, Evas_Object * obj, void *event_info)
{
	char *label;
	number_points++;
	sprintf(label,"%d",number_points);
	elm_object_text_set(entry_points, label);
}

static void click_minus(void *data, Evas_Object * obj, void *event_info)
{
	char *label;
	number_points++;
	sprintf(label,"%d",number_points);
	elm_object_text_set(entry_points, label);
}

EAPI int elm_main(int argc, char **argv)
{
	int i = 0;
	if (argc > 1) {
		if (!strcmp(argv[1], "help")) {
			printf
			    ("Usage:\nfocus_stack [ help | sweep | number_of_photos [ delay_between_photos [ button_height [ button_width ] ] ] ]\n\n");
			exit(0);
		}
		if (!strcmp(argv[1], "sweep")) {
		} else {
			number_points = atoi(argv[1]);
		}
		if (argc > 2) {
			shot_delay = atoi(argv[2]);
		}
		if (argc > 3) {
			button_height = atoi(argv[3]);
		}
		if (argc > 4) {
			button_width = atoi(argv[4]);
		}
	}
	
	printf("Stacking with %d photos at %d delay and %d button height\n",number_points, shot_delay, button_height);

	win = elm_win_add(NULL, "Focus stacker", ELM_WIN_BASIC);
	evas_object_move(win, 60, 0);
	evas_object_smart_callback_add(win, "delete,request",
				       click_quit, NULL);

	box = elm_box_add(win);
	elm_box_horizontal_set(box, EINA_TRUE);
	elm_win_resize_object_add(win, box);
	evas_object_show(box);
	table = elm_table_add(win);
	elm_box_pack_end(box, table);

	Evas_Object *bg;
	evas_object_size_hint_min_set(box, SCREEN_WIDTH, button_height);

	btn_near = elm_button_add(win);
	elm_object_style_set(btn_near, "transparent");
	elm_object_text_set(btn_near, "Near");
	evas_object_show(btn_near);
	evas_object_size_hint_min_set(btn_near, button_width, button_height);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn_near));
	evas_object_size_hint_min_set(bg, button_width, button_height);
	evas_object_color_set(bg, 40, 60, 80, 128);
	evas_object_show(bg);
	elm_table_pack(table, bg, 1, 1, 1, 1);
	elm_table_pack(table, btn_near, 1, 1, 1, 1);

	evas_object_smart_callback_add(btn_near, "clicked", click_near, NULL);

	btn_far = elm_button_add(win);
	elm_object_style_set(btn_far, "transparent");
	elm_object_text_set(btn_far, "Far");
	evas_object_show(btn_far);
	evas_object_size_hint_min_set(btn_far, button_width, button_height);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn_far));
	evas_object_size_hint_min_set(bg, button_width, button_height);
	evas_object_color_set(bg, 20, 40, 60, 128);
	evas_object_show(bg);
	elm_table_pack(table, bg, 2, 1, 1, 1);
	elm_table_pack(table, btn_far, 2, 1, 1, 1);

	evas_object_smart_callback_add(btn_far, "clicked", click_far, NULL);

	btn_stack = elm_button_add(win);
	elm_object_style_set(btn_stack, "transparent");
	elm_object_text_set(btn_stack, "Stack");
	evas_object_show(btn_stack);
	evas_object_size_hint_min_set(btn_stack, button_width, button_height);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn_stack));
	evas_object_size_hint_min_set(bg, button_width, button_height);
	evas_object_color_set(bg, 40, 60, 80, 128);
	evas_object_show(bg);
	elm_table_pack(table, bg, 6, 1, 1, 1);
	elm_table_pack(table, btn_stack, 6, 1, 1, 1);

	evas_object_smart_callback_add(btn_stack, "clicked", click_stack, NULL);

	btn_quit = elm_button_add(win);
	elm_object_style_set(btn_quit, "transparent");
	elm_object_text_set(btn_quit, " X ");
	evas_object_show(btn_quit);
	evas_object_size_hint_min_set(btn_quit, 80, button_height);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn_quit));
	evas_object_size_hint_min_set(bg, 80, button_height);
	evas_object_color_set(bg, 80, 40, 20, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg, 7, 1, 1, 1);
	elm_table_pack(table, btn_quit, 7, 1, 1, 1);

	evas_object_smart_callback_add(btn_quit, "clicked", click_quit, NULL);

	evas_object_show(table);
	evas_object_show(win);

	evas_object_event_callback_add(win, EVAS_CALLBACK_FOCUS_IN,restore_touch, NULL);

	elm_run();
	return 0;
}

ELM_MAIN()