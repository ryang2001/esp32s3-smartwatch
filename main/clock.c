#include "clock.h"
#include <time.h>
#include <stdio.h>

void set_needles()
{
    time_t timer;//time_t就是long int 类型
　　struct tm *tblock;
　　timer = time(NULL);
　　tblock = localtime(&timer);
　　printf("Local time is: %s\n",asctime(tblock));
    /*
	//add needle line for clock_meter_2_scale_1
	lv_meter_indicator_t *clock_meter_2_scale_1_ndline_0;
	clock_meter_2_scale_1_ndline_0 = lv_meter_add_needle_line(ui->clock_meter_2, clock_meter_2_scale_1, 2, lv_color_make(0xf2, 0x6d, 0x07), 0);
	lv_meter_set_indicator_value(ui->clock_meter_2, clock_meter_2_scale_1_ndline_0, 1);

	//add needle line for clock_meter_2_scale_1
	lv_meter_indicator_t *clock_meter_2_scale_1_ndline_1;
	clock_meter_2_scale_1_ndline_1 = lv_meter_add_needle_line(ui->clock_meter_2, clock_meter_2_scale_1, 5, lv_color_make(0x00, 0x00, 0x00), -10);
	lv_meter_set_indicator_value(ui->clock_meter_2, clock_meter_2_scale_1_ndline_1, 2);

	//add needle line for clock_meter_2_scale_1
	lv_meter_indicator_t *clock_meter_2_scale_1_ndline_2;
	clock_meter_2_scale_1_ndline_2 = lv_meter_add_needle_line(ui->clock_meter_2, clock_meter_2_scale_1, 5, lv_color_make(0x00, 0x00, 0x00), -40);
	lv_meter_set_indicator_value(ui->clock_meter_2, clock_meter_2_scale_1_ndline_2, 3);
    */
}