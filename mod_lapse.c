/*
* Usage: 
* 
* 
* Compile with:
*  
* arm-linux-gnueabi-gcc -o mod_lapse mod_lapse.c `pkg-config --cflags --libs ecore elementary` --sysroot=../arm/ -Wl,-dynamic-linker,/lib/ld-2.13.so
* We need to specify the correct ld or it will not work on device.
*/
#define _GNU_SOURCE
#include <Elementary.h>
#include <strings.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/msg.h>
#include <errno.h>
#include <math.h>

#define SCREEN_WIDTH 720
#define SCREEN_HEIGHT 480

// default debug
static int debug=1;

// for EV Smoothing
static int Tv, ev_step=0, ev_rolling=1, ev_steps=5, threshold=4;

static Eina_Bool is_video=EINA_FALSE, lcd_off=EINA_FALSE, lcd_show_capture=EINA_FALSE, after_gui=EINA_FALSE, after_off=EINA_FALSE, save_settings=EINA_FALSE, smooth_ev=EINA_FALSE;
static Evas_Object *black, *win, *bg, *bg2, *box, *btn, *chk, *table, *entry_period, *entry_duration, *entry_shots, *status_display, *entry_start_delay;
pthread_t timer_thread, force_update_thread, sleep_detect_thread, ev_smooth_thread;
char *input_duration,*input_period, *input_shots, *input_start_delay;
int button_height = 80, button_width = 360, button_number = 0;
char stringline[255], label_entry[255], sample_text[255];
static int ev_values[255];
static char *version_model, *version_release, *lapse_command;
Evas_Object *popup_win;
Ecore_Timer *timer;

static int a_type, a_number=3, a_lcd_black, a_duration, a_show_timer, a_after_gui, a_after_off, a_start_delay, a_save_settings, a_scroll_info=1, a_ev_smooth=0;
static float a_delay=3.0;

static char *settings_file = "/root/mod_lapse.cfg";
static char *capture_message = "app nx capture single";

static void run_command(char *command)
{
	if (debug) printf("CMD: %s", command);
	char *cmd;
	asprintf(&cmd, "%s", command);
	system(cmd);
	free(cmd);
	if (debug) printf(" done.\n");
}

int send_message(char * message_in){
	int i=0, result, fd=-1;
	unsigned char message_text[208]="\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
	char  *param, *message, *spl;
	struct msg_buf_out {
		long mtype;
		char mtext[208];
	} msg;
	if (message_in[0]=='/')
		asprintf(&message,"%s",message_in);
	else
		asprintf(&message,"/usr/bin/st %s",message_in);
	
	spl=strtok(message," ");
	
	while(spl && strlen(spl)>0) {
		if (debug>2) printf("Adding at %d %s(%d)\n",4+i*20,spl,(int)strlen(spl));
		memcpy(message_text+4+i*20,spl,strlen(spl));
		i++;
		spl = strtok(NULL, " ");
	}
	asprintf(&spl,"%d",i);
	memcpy(message_text,spl, strlen(spl));

	if (debug>1) {
		printf("Message:\n");
		for(i=0;i<208;i++) {
			if ((i-4)%20==0) printf("\n");
			if (message_text[i]==0) 
				printf("_"); 
			else 
				printf("%c",(char)message_text[i]);
		}
		printf("\n");
	}

	if (fd<1) {
		fd = msgget(0x8828, 0666);
		if (fd<0) {
			perror(strerror(errno));
			printf("ERROR %d %d\n",errno, fd);
			return -1;
		}
	}
	msg.mtype=1;
	memcpy(msg.mtext, message_text, 208);
	result = msgsnd(fd, &msg, 208, 0);
	if (result<0) {
		perror( strerror(errno) );
		return -1;
	}
	return result;
}

long msec_passed(struct timeval *fromtime, struct timeval *totime)
{
  long msec;
  msec=(totime->tv_sec-fromtime->tv_sec)*1000;
  msec+=(totime->tv_usec-fromtime->tv_usec)/1000;
  return msec;
}

// Detect sleep events
void* timer_loop(void* arg) {
	struct timeval previous_time_loop, current_time_loop;
	int timer_sleep=1000;
	long msec_elapsed;
	while(1) {
		gettimeofday(&previous_time_loop, NULL); 
		usleep(timer_sleep*1000);
		gettimeofday(&current_time_loop, NULL); 
		msec_elapsed = msec_passed(&previous_time_loop,&current_time_loop);
		if (msec_elapsed>(timer_sleep+100)) exit(1);
	}
}

static unsigned int get_af_mode()
{
	FILE *fp;
	char *spl = NULL;
	fp = popen("/usr/bin/st cap capdtm getusr AFMODE", "r");
	if (fp == NULL) {
		if (debug) printf("Failed get current focus mode\n");
		return 0;
	} else {
		if (fgets(stringline, sizeof(stringline) - 1, fp) != NULL) {
			stringline[0] = '_';	// fix st output
			spl = strtok(stringline, "(");
			spl = strtok(NULL, ")");
		}
		pclose(fp);
		if (debug) printf("Current focus mode: %s (%ld)\n", spl,strtol(spl, NULL, 16));
		return strtol(spl, NULL, 16);
	}
}

static void set_af_mode(unsigned int af_mode)
{
	char * af_mode_s;
	asprintf(&af_mode_s,"0x%06x",af_mode);
	if (af_mode_s[strlen(af_mode_s)-1]=='0') run_command("/usr/bin/st app nx capture af-mode single");
	if (af_mode_s[strlen(af_mode_s)-1]=='1') run_command("/usr/bin/st app nx capture af-mode caf");
	if (af_mode_s[strlen(af_mode_s)-1]=='3') {
		run_command("/usr/bin/st app nx capture af-mode manual");	// show manual focus mode
		usleep(300000);
		run_command("/usr/bin/st cap capdtm setusr AFMODE 0x70003");	// force manual focus mode
		usleep(300000);
	}
	if (af_mode_s[strlen(af_mode_s)-1]=='4') run_command("/usr/bin/st app nx capture af-mode auto");
	sprintf(stringline, "/usr/bin/st cap capdtm setusr AFMODE 0x%06x", af_mode);
	run_command(stringline);
}

void ev_smooth_clean(int steps, int value) {
	int i;
	if (value==0) {
		for(i=0;i<steps;i++) ev_values[i]=value;
	} else {
		for(i=0;i<steps;i++) ev_values[i]+=value;
	}
}

void do_ev_smooth(int steps) {
	int ev_get() {
		FILE *fp;
		char *spl = "";
		int Ev=0;
		
		if (debug==2) {
			fp = popen("cat ../exposure.txt", "r");
		} else {
			fp = popen("/bin/bash -c \"/usr/bin/st key push s1; /usr/bin/st key release s1; /usr/bin/st cap lockinfo\"", "r");
		}
		
		if (fp == NULL) {
			if (debug) printf("Failed to get current EV data\n");
			return 0;
		} else {
			if ((fgets(stringline, sizeof(stringline) - 1, fp) != NULL) && (fgets(stringline, sizeof(stringline) - 1, fp) != NULL) &&(fgets(stringline, sizeof(stringline) - 1, fp) != NULL)) {
				stringline[0] = '_';	// fix st output
				spl = strtok(stringline, ":");
				spl = strtok(NULL, ",");
				Tv=atoi(spl);
				if (fgets(stringline, sizeof(stringline) - 1, fp) != NULL) {
					stringline[0] = '_';	// fix st output
					spl = strtok(stringline, ":");
					spl = strtok(NULL, " ");
					Ev=atoi(spl);
					if (debug) printf("Tv: %d\tEv: %d\n",Tv,Ev);
				}
			} else {
				printf("Error reding from file. Stringline: %s\n",stringline);
			}
			pclose(fp);
			usleep(500000);
			if (debug==2) {
				return 16-rand()%20;
			}
			return Ev;
		}
	}

	int i, sum=0, deltaEv=0, newTv=0;
	float avg=0.0;
	char *cmd;
	ev_values[ev_step]=ev_get();
	for(i=0;i<steps;i++) {
		sum+=ev_values[i];
		printf("%d\t",ev_values[i]);
	}
	printf("\n");
	avg=(float)sum/steps;
	printf("AVG: %f\n",avg);
	if ((abs(avg)+0.05)>threshold) {
		sum=0;
		for(i=0;i<steps;i++) {
			if ((avg>0 && ev_values[i]>=threshold) || (avg<0 && ev_values[i]<=threshold)) sum++;
		}
		if (sum>(steps/2)) {
			deltaEv = (int)round(avg/abs(avg))*threshold;
			newTv = Tv + deltaEv;
			printf("Adjusting shutter by %d to %d (%d points above %d)\n",deltaEv, newTv, sum, steps/2);
			if (debug<2) {
				asprintf(&cmd,"st cap capdtm setvar TV %d 4",newTv);
				sleep(1);
				run_command(cmd);
			}
			if (ev_rolling==1) ev_smooth_clean(steps, -deltaEv);
			else ev_smooth_clean(steps, 0);
		} 
	}
	ev_step++;
	if (ev_step>=steps) {
		ev_step=0;
	}
}

static void quit_app()
{
	if (debug) printf("Exiting the app.\n");
	elm_exit();
	exit(0);

}

static void do_save_settings(int is_video, int shots, float delay, int duration, int lcd_black, int lcd_show, int after_off, int after_gui, int start_delay, int ev_smooth)
{
	FILE *f = fopen(settings_file, "w");
	if (f != NULL) {
		fprintf(f, "%d %d %f %d %d %d %d %d %d %d\n", is_video, shots, delay, duration, lcd_black, lcd_show, after_off, after_gui, start_delay, ev_smooth);
		fclose(f);
	}
}

static void do_load_settings()
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	fp = fopen(settings_file, "r");
	if (fp != NULL) {
		if (debug) printf("Reading configuration... ");
		if ((read = getline(&line, &len, fp)) != -1) {
			sscanf(line,  "%d %d %f %d %d %d %d %d %d %d", &a_type, &a_number, &a_delay, &a_duration, &a_lcd_black, &a_show_timer, &a_after_off, &a_after_gui, &a_start_delay, &a_ev_smooth);
		}
		fclose(fp);
		free(line);
		if (debug) printf ("Loaded: %d %d %f %d %d %d %d %d %d %d\n", a_type, a_number, a_delay, a_duration, a_lcd_black, a_show_timer, a_after_off, a_after_gui, a_start_delay, a_ev_smooth);
	} else {
		printf ("Cannot access %s configuration file.\n",settings_file);
	}
}


static void click_quit(void *data, Evas_Object * obj, void *event_info)
{
	quit_app();
}

static int version_load()
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	fp = fopen("/etc/version.info", "r");
	if (fp != NULL) {
		if ((read = getline(&line, &len, fp)) != -1) {
			line[strcspn(line, "\r\n")] = 0;
			asprintf(&version_release, "%s", line);
		}
		if ((read = getline(&line, &len, fp)) != -1) {
			line[strcspn(line, "\r\n")] = 0;
			asprintf(&version_model, "%s", line);
		}
		fclose(fp);
		free(line);
		return 0;
	}
	printf("Unable to determine device model and firmware version!\n");
	quit_app();
	return -1;
}

static Eina_Bool key_down_callback(void *data, int type, void *ev)
{
	Ecore_Event_Key *event = ev;
	if (debug) printf("Key: %s\n", event->key);
	if ((0 == strcmp("Super_R", event->key) ||
		0 == strcmp("Menu", event->key)||
		0 == strcmp("Super_L", event->key)))
	{
		quit_app();
	}
	
	if (0 == strcmp("XF86PowerOff", event->key)) {
		evas_object_hide(win);
		system("st key click pwoff");
	}
	return ECORE_CALLBACK_PASS_ON;
}

static void show_black()
{
	if (debug) printf("Black ON\n");
	black = elm_win_add(NULL, "Otto-Lapse Black", ELM_WIN_NOTIFICATION);
	elm_win_raise(black);
	elm_win_prop_focus_skip_set(black, EINA_TRUE); // we want key events to ignore this window
	evas_object_smart_callback_add(black, "delete,request", click_quit, NULL);
	evas_object_size_hint_min_set(black, SCREEN_WIDTH, SCREEN_HEIGHT);
	box = elm_box_add(black);
	elm_win_resize_object_add(black, box);
	evas_object_size_hint_min_set(box, SCREEN_WIDTH, SCREEN_HEIGHT);
	evas_object_show(black);
	evas_object_show(box);

	table = elm_table_add(black);
	elm_box_pack_end(box, table);
	evas_object_show(table);


	status_display = elm_entry_add(black);
	elm_entry_single_line_set(status_display, EINA_TRUE);
	elm_object_text_set(status_display, "");
	evas_object_show(status_display);
	Evas_Object *bg;
	bg = evas_object_rectangle_add(evas_object_evas_get(status_display));
	if (1==a_lcd_black) 
		evas_object_size_hint_min_set(bg, SCREEN_WIDTH, SCREEN_HEIGHT);
	else {
		a_scroll_info=0;
		evas_object_size_hint_min_set(bg, 480, 80);
	}
	evas_object_color_set(bg, 0,0,0,255);
	evas_object_show(bg);

	elm_table_pack(table, bg, 1, 1, 1, 1);
	elm_table_pack(table, status_display, 1, 1, 1, 1);
	elm_entry_text_style_user_push(status_display, "DEFAULT='color=#f00 align=center'");

}

void* force_update(void* arg) {
	ecore_main_loop_iterate();
	return NULL;
}


static void do_capture(int type, int number, float delay, int lcd_black, int duration, int show_timer, int after_off, int after_gui, int start_delay, int ev_smooth) {
	int i;
	char *status_message,*prefix;
	struct timeval previous_time, current_time;
	long msec_elapsed, sleep_for;
	
	if (ev_smooth) {
		printf("Entering M mode\n");
		send_message("key mode m");
		usleep(300000);
	}
	int af_mode = get_af_mode();
	set_af_mode(3);
	
	ev_smooth_clean(ev_steps,0);
	
	printf("Capture Video: %d\t Number: %d\tDelay: %f\tDuration: %d\tWait: %d\tBlackout: %d\tTimer: %d\t After OFF:%d\tAfter GUI:%d\tEV Smooth:%d\n",
		   type, number, delay, duration, start_delay, lcd_black, show_timer, after_off, after_gui, ev_smooth);
	pthread_create(&sleep_detect_thread, NULL, &timer_loop, NULL);
	if (0!=type) run_command("/usr/bin/st key click del");
	if (delay<0.99) delay=1.0;
	if (1==lcd_black) {
		//Turn QuickView OFF
		if (0==strcmp("NX500",version_model)) {
			system("prefman set 0 0x0210 l 0;st cap capdtm setusr 51 0x0330000");
		}
		if (0==strcmp("NX1",version_model)) {
			system("prefman set 0 0x0210 l 0;st cap capdtm setusr 49 0x0310000");
		}
	}
	if (show_timer) {
		for(i=0;i<start_delay;i++) {
			asprintf(&status_message,"Waiting for %d s",start_delay-i);
			elm_object_text_set(status_display,status_message);
			pthread_create(&force_update_thread, NULL, &force_update, NULL);
			run_command("sleep 1");
		}
	} else {
		sleep(start_delay);
	}
	asprintf(&prefix," ");
	for (i=0;i<number;i++) {
		gettimeofday(&previous_time, NULL); 
		if (i % 10 > 0 && a_scroll_info==1) 
			asprintf(&prefix,"<br>%s",prefix);
		else 
			asprintf(&prefix," ");
		if (0==type) {
			//run_command("/usr/bin/st cap capt single start"); // for a quick shot but just single - no bracketing, etc.
			//run_command("/usr/bin/st app nx capture single");
			send_message(capture_message); // much faster execution
		} else {
			if (show_timer) {
				asprintf(&status_message,"%s\ncapt: %d/%d  video: %ds",prefix,i+1,number,duration);
				elm_object_text_set(status_display,status_message);
				pthread_create(&force_update_thread, NULL, &force_update, NULL);
			}
			run_command("/usr/bin/st app nx record start");
			sleep(duration);
			run_command("/usr/bin/st app nx record stop");
		}
		if (show_timer) {
			asprintf(&status_message,"%s\ncapt: %d/%d  wait: %f s",prefix,i+1,number,delay);
			elm_object_text_set(status_display,status_message);
			pthread_create(&force_update_thread, NULL, &force_update, NULL);
		}
		if (i < (number-1)) {
			if (ev_smooth) do_ev_smooth(ev_steps);
			gettimeofday(&current_time, NULL); 
			msec_elapsed = msec_passed(&previous_time,&current_time);
			sleep_for=delay*1000000-msec_elapsed*1000;
			if (sleep_for<0) sleep_for=0;
			usleep(sleep_for); 
		} else if (show_timer) {
			asprintf(&status_message,"%s Lapse complete %d/%d",prefix,i+1,number);
			elm_object_text_set(status_display,status_message);
			pthread_create(&force_update_thread, NULL, &force_update, NULL);
			sleep(3);
		}
	}
	set_af_mode(af_mode);
	if (1==after_off) {
		printf("Shutting down in 3 s ...\n");
		system("sync;sync;sync;sleep 3;echo -n disk > /sys/power/state");
	}
	quit_app();
}

static void click_save_settings(void *data, Evas_Object *obj, void *event_info)
{
	int shots,video,duration,lcd_black,lcd_show,start_delay;
	float delay;
	shots=atoi(elm_object_text_get(entry_shots));
	delay=atof(elm_object_text_get(entry_period));
	video=(int)is_video;
	duration=atoi(elm_object_text_get(entry_duration));
	start_delay=atoi(elm_object_text_get(entry_start_delay));
	lcd_black=(int)lcd_off;
	lcd_show=(int)lcd_show_capture;
	printf("Saving settings...");
	do_save_settings(video, shots, delay, duration, lcd_black, lcd_show, (int)after_off, (int)after_gui, (int)start_delay, (int)smooth_ev);
	printf(" done.\n");
}

static void start_capture(void *data, Evas_Object *obj, void *event_info)
{
	int shots,video,duration,lcd_black,lcd_show,start_delay;
	float delay;
	char * command;
	evas_object_hide(win);
	shots=atoi(elm_object_text_get(entry_shots));
	delay=atof(elm_object_text_get(entry_period));
	video=(int)is_video;
	duration=atoi(elm_object_text_get(entry_duration));
	start_delay=atoi(elm_object_text_get(entry_start_delay));
	lcd_black=(int)lcd_off;
	lcd_show=(int)lcd_show_capture;
	if (0==video) {
		asprintf(&command, "%s photo %d %f %d %d %d %d %d %d", lapse_command, shots, delay, lcd_black, lcd_show, (int)after_off, (int)after_gui, (int)start_delay, (int)smooth_ev);
	}
	if (1==video) {
		asprintf(&command, "%s video %d %f %d %d %d %d %d %d %d", lapse_command, shots, delay, duration, lcd_black, lcd_show, (int)after_off, (int)after_gui, (int)start_delay, (int)smooth_ev);
	}
	run_command(command);
	if (1==after_gui) {
		evas_object_show(win);
	} else {
		quit_app();
	}
}

static void lose_focus(void *data, Evas_Object *obj, void *event_info)
{
	elm_object_focus_set(obj, EINA_FALSE);
}

static void force_focus(void *data, Evas_Object *obj, void *event_info)
{
	elm_object_focus_set((Evas_Object *)data, EINA_TRUE);
}

// Main GUI screen
void show_main()
{
	int row=0, col=0;
	
	if (debug) printf("Running mod_lapse\n");
	
	win = elm_win_add(NULL, "Otto-Lapse GUI", ELM_WIN_BASIC);
	evas_object_smart_callback_add(win, "delete,request", click_quit, NULL);
	box = elm_box_add(win);
	elm_win_resize_object_add(win, box);
	evas_object_show(box);
	table = elm_table_add(win);
	elm_box_pack_end(box, table);
	evas_object_size_hint_min_set(box, SCREEN_WIDTH, SCREEN_HEIGHT);

	// START BUTTON
	row=1; col=1;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Shots #:                          ");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	entry_shots = elm_entry_add(win);
	elm_entry_single_line_set(entry_shots, EINA_TRUE);
	elm_object_text_set(entry_shots, input_shots);
	elm_object_style_set(entry_shots, "transparent");
// 	evas_object_size_hint_min_set(entry_shots, button_width, button_height);
	evas_object_show(entry_shots);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, entry_shots, col, row, 1, 1);
	elm_entry_cursor_pos_set(entry_shots, strlen(input_shots));
	elm_entry_text_style_user_push(entry_shots, "DEFAULT='align=right'");
	elm_entry_input_panel_layout_set(entry_shots,ELM_INPUT_PANEL_LAYOUT_NUMBERONLY);
	evas_object_smart_callback_add(entry_shots, "activated",
						       lose_focus,
						       NULL);
	evas_object_smart_callback_add(btn, "clicked",
					force_focus,
					entry_shots);

	// END BUTTON
	
	// START BUTTON
	row=1; col=2;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Delay s:                          ");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	entry_period = elm_entry_add(win);
	elm_entry_single_line_set(entry_period, EINA_TRUE);
	elm_object_text_set(entry_period, input_period);
	elm_object_style_set(entry_period, "transparent");
	evas_object_size_hint_min_set(entry_period, button_width, button_height);
	evas_object_show(entry_period);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(entry_period));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(entry_period));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, entry_period, col, row, 1, 1);
	elm_entry_cursor_pos_set(entry_period, strlen(input_period));
	elm_entry_text_style_user_push(entry_period, "DEFAULT='align=right'");
	elm_entry_input_panel_layout_set(entry_period,ELM_INPUT_PANEL_LAYOUT_NUMBER);
	evas_object_smart_callback_add(entry_period, "activated",
						       lose_focus,
						       NULL);
	evas_object_smart_callback_add(btn, "clicked",
					force_focus,
					entry_period);
//	elm_entry_text_style_user_push(btn, "DEFAULT='font=Tizen:style=Light font_size=30 color=#8df align=center'");
	// END BUTTON
	
	// START BUTTON
	row=2; col=1;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Video s:                          ");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	entry_duration = elm_entry_add(win);
	elm_entry_single_line_set(entry_duration, EINA_TRUE);
	elm_object_text_set(entry_duration, input_duration);
	elm_object_style_set(entry_duration, "transparent");
	evas_object_size_hint_min_set(entry_duration, button_width, button_height);
	evas_object_show(entry_duration);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(entry_duration));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(entry_duration));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 80, 40, 40, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, entry_duration, col, row, 1, 1);
	elm_entry_cursor_pos_set(entry_duration, strlen(input_duration));
	elm_entry_text_style_user_push(entry_duration, "DEFAULT='align=right'");
	elm_entry_input_panel_layout_set(entry_duration,ELM_INPUT_PANEL_LAYOUT_NUMBERONLY);
	evas_object_smart_callback_add(entry_duration, "activated",
						       lose_focus,
						       NULL);
	evas_object_smart_callback_add(btn, "clicked",
					force_focus,
					entry_duration);
	// END BUTTON

	// START BUTTON
	row=2; col=2;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Wait s:                          ");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	entry_start_delay = elm_entry_add(win);
	elm_entry_single_line_set(entry_start_delay, EINA_TRUE);
	elm_object_text_set(entry_start_delay, input_start_delay);
	elm_object_style_set(entry_start_delay, "transparent");
	evas_object_size_hint_min_set(entry_start_delay, button_width, button_height);
	evas_object_show(entry_start_delay);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(entry_start_delay));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(entry_start_delay));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, entry_start_delay, col, row, 1, 1);
	elm_entry_text_style_user_push(entry_start_delay, "DEFAULT='align=right'");
	elm_entry_input_panel_layout_set(entry_start_delay,ELM_INPUT_PANEL_LAYOUT_NUMBERONLY);
	elm_entry_cursor_pos_set(entry_start_delay, strlen(input_start_delay));
	evas_object_smart_callback_add(entry_start_delay, "activated",
						       lose_focus,
						       NULL);
	evas_object_smart_callback_add(btn, "clicked",
					force_focus,
					entry_start_delay);
	// END BUTTON
	
	// START BUTTON
	row=3; col=1;
	chk = elm_check_add(win);
	elm_object_style_set(chk, "transparent");
	evas_object_show(chk);
	evas_object_size_hint_min_set(chk, button_width,
						button_height);
	elm_check_state_pointer_set(chk, &is_video);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Record Videos");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 80, 40, 40, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, chk, col, row, 1, 1);
	// END BUTTON
	
	// START BUTTON
	row=3;col=2;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width,
						button_height);
	elm_check_state_pointer_set(btn, &save_settings);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Save settings");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 90, 120, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	evas_object_smart_callback_add(btn, "clicked",
					click_save_settings,
					NULL);

	// END BUTTON

	// START BUTTON
	row=4; col=1;
	chk = elm_check_add(win);
	elm_object_style_set(chk, "transparent");
	evas_object_show(chk);
	evas_object_size_hint_min_set(chk, button_width,
						button_height);
	elm_check_state_pointer_set(chk, &lcd_off);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "LCD Black Out");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, chk, col, row, 1, 1);
	// END BUTTON
	
	// START BUTTON
	col=2;
	chk = elm_check_add(win);
	elm_object_style_set(chk, "transparent");
	evas_object_show(chk);
	evas_object_size_hint_min_set(chk, button_width,
						button_height);
	elm_check_state_pointer_set(chk, &lcd_show_capture);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "LCD Show Count");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, chk, col, row, 1, 1);
	// END BUTTON

	// START BUTTON
	row++; col=1;
	chk = elm_check_add(win);
	elm_object_style_set(chk, "transparent");
	evas_object_show(chk);
	evas_object_size_hint_min_set(chk, button_width,
						button_height);
	elm_check_state_pointer_set(chk, &after_off);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "OFF after lapse");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, chk, col, row, 1, 1);
	// END BUTTON
	
	// START BUTTON
	col=2;
	chk = elm_check_add(win);
	elm_object_style_set(chk, "transparent");
	evas_object_show(chk);
	evas_object_size_hint_min_set(chk, button_width,
						button_height);
	elm_check_state_pointer_set(chk, &smooth_ev);
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "Smooth EV");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 60, 80, 120, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	elm_table_pack(table, chk, col, row, 1, 1);
	// END BUTTON

	

	// START BUTTON
	row=6; col=1;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "QUIT!");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 120, 0, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	evas_object_smart_callback_add(btn, "clicked",
					click_quit,
					NULL);

	// END BUTTON
	
	// START BUTTON
	col=2;
	btn = elm_button_add(win);
	elm_object_style_set(btn, "transparent");
	elm_object_text_set(btn, "START!");
	evas_object_show(btn);
	evas_object_size_hint_min_set(btn, button_width, button_height);
	bg2 = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg2, button_width, button_height);
	evas_object_color_set(bg2, 30, 40, 50, 255);
	evas_object_show(bg2);
	bg = evas_object_rectangle_add(evas_object_evas_get(btn));
	evas_object_size_hint_min_set(bg, button_width - 2,
						button_height - 2);
	evas_object_color_set(bg, 0, 120, 0, 255);
	evas_object_show(bg);
	elm_table_pack(table, bg2, col, row, 1, 1);
	elm_table_pack(table, bg, col, row, 1, 1);
	elm_table_pack(table, btn, col, row, 1, 1);
	evas_object_smart_callback_add(btn, "clicked",
				start_capture,
				NULL);
	// END BUTTON
	
	evas_object_show(table);
	evas_object_show(win);
	ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, key_down_callback, NULL);
}

void* timer_capture(void* arg) {
	do_capture(a_type,a_number,a_delay,a_lcd_black,a_duration,a_show_timer, a_after_off, a_after_gui, a_start_delay, a_ev_smooth);
	return NULL;
}

// MAIN BEGIN
EAPI int elm_main(int argc, char **argv)
{
	lapse_command = argv[0];
	version_load();
	if (strcmp(argv[argc - 1], "debug") == 0) {
		debug = 1;
	}
	
	if (debug) printf("Debug ON\n");
	if (argc > 1 && strcmp("gui",argv[1])!=0) {
		if (strcmp(argv[1], "help") == 0) {
			printf("Usage:\n%s [options] [debug]\n\n\tdebug - if it is present debugging is on\n\n",argv[0]);
			printf("Usage:\n%s [gui] - Show graphical user interface to configure capture\n",argv[0]);
			printf("Usage:\n%s photo number delay lcd_black show_timer after_off after_gui start_delay - Capture *number* of photos with *delay* seconds between captures after *start_delay* seconds and black out the LCD if *lcd_black* is 1 and show timer if *show_timer* is 1. After capture turn off if *after_off* is 1 or return to GUI if *after_gui* is 1.\n",argv[0]);
			printf("Usage:\n%s video number delay duration lcd_black show_timer after_off after_gui start_delay - Capture *number* of videos of *duration* seconds with *delay* seconds between captures after *start_delay* seconds and black out the LCD if *lcd_black* is 1 and show timer if *show_timer* is 1. After capture turn off if *after_off* is 1 or return to GUI if *after_gui* is 1.\n",argv[0]);
			exit(0);
		}
		if (strcmp(argv[1], "photo") == 0) {
			a_type=0;
			a_number = atoi(argv[2]);
			a_delay = atof(argv[3]);
			a_lcd_black = atoi(argv[4]);
			a_show_timer = atoi(argv[5]);
			a_after_off = atoi(argv[6]);
			a_after_gui = atoi(argv[7]);
			a_start_delay = atoi(argv[8]);
			a_ev_smooth = atoi(argv[9]);
			a_duration=0;
			pthread_create(&timer_thread, NULL, &timer_capture, NULL);
			if (1==a_lcd_black || 1==a_show_timer) show_black();
		}
		if (strcmp(argv[1], "video") == 0) {
			a_type=1;
			a_number = atoi(argv[2]);
			a_delay = atof(argv[3]);
			a_duration = atoi(argv[4]);
			a_lcd_black = atoi(argv[5]);
			a_show_timer = atoi(argv[6]);
			a_after_off = atoi(argv[7]);
			a_after_gui = atoi(argv[8]);
			a_start_delay = atoi(argv[9]);
			a_ev_smooth = atoi(argv[10]);
			pthread_create(&timer_thread, NULL, &timer_capture, NULL);
			if (1==a_lcd_black || 1==a_show_timer) show_black();
		}
	} else {
		do_load_settings();
		asprintf(&input_shots,"%d",a_number);
		asprintf(&input_period,"%.1f",a_delay);
		asprintf(&input_duration,"%d",a_duration);
		asprintf(&input_start_delay,"%d",a_start_delay);
		is_video=a_type;
		smooth_ev=a_ev_smooth;
		save_settings=a_save_settings;
		after_gui=a_after_gui;
		after_off=a_after_off;
		lcd_off=a_lcd_black;
		lcd_show_capture=a_show_timer;
		show_main();
	}

	elm_run();
	return 0;
}

ELM_MAIN()
// MAIN END
