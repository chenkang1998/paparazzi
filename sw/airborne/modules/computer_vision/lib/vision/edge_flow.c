/*
 * edge_flow.c
 *
 *  Created on: Feb 22, 2016
 *      Author: knmcguire
 */
#include <lib/vision/edge_flow.h>

static uint32_t timeval_diff2(struct timeval *starttime, struct timeval *finishtime);

void test_function(struct image_t *img,struct image_t *img_gray)
{
	image_to_grayscale(img, img_gray);

}




// calculate_edge_histogram calculates the image gradient of the images and makes a edge feature histogram
void calculate_edge_histogram(struct image_t *img, int32_t edge_histogram[],
		char direction, uint16_t edge_threshold)
{
	uint8_t *img_buf = (uint8_t *)img->buf;

	// TODO use arm_conv_q31()
	int32_t sobel_sum = 0;
	int32_t Sobel[3] = { -1, 0, 1};

	uint32_t y = 0, x = 0;
	int32_t c = 0;

	uint32_t idx = 0;

	uint16_t image_width = img->w;
	uint16_t image_height = img->h;
	uint32_t interlace;
	if(img->type == IMAGE_GRAYSCALE)
		interlace = 1;
	else {
		if(img->type == IMAGE_YUV422)
			interlace = 2;
		else
			while (1);   // hang to show user something isn't right
	}


	// compute edge histogram
	if (direction == 'x') {
		// set values that are not visited
		edge_histogram[0] = edge_histogram[image_width - 1] = 0;
		for (x = 1; x < image_width - 1; x++) {
			edge_histogram[x] = 0;
			for (y = 0; y < image_height; y++) {
				sobel_sum = 0;

				for (c = -1; c <= 1; c++) {
					idx = interlace * (image_width * y + (x + c)); // 2 for interlace

					sobel_sum += Sobel[c + 1] * (int32_t)img_buf[idx+1];
				}
				sobel_sum = abs(sobel_sum);
				if (sobel_sum > edge_threshold) {
					edge_histogram[x] += sobel_sum;
				}
			}
		}
	} else if (direction == 'y') {
		// set values that are not visited
		edge_histogram[0] = edge_histogram[image_height - 1] = 0;
		for (y = 1; y < image_height - 1; y++) {
			edge_histogram[y] = 0;
			for (x = 0; x < image_width; x++) {
				sobel_sum = 0;

				for (c = -1; c <= 1; c++) {
					idx = interlace * (image_width * (y + c) + x); // 2 for interlace

					sobel_sum += Sobel[c + 1] * (int32_t)img_buf[idx+1];
				}
				sobel_sum = abs(sobel_sum);
				if (sobel_sum > edge_threshold) {
					edge_histogram[y] += sobel_sum;
				}
			}
		}
	} else
		while (1);  // hang to show user something isn't right
}

// Calculate_displacement calculates the displacement between two histograms
// D should be half the search disparity range
// W is local search window
void calculate_edge_displacement(int32_t *edge_histogram, int32_t *edge_histogram_prev, int32_t *displacement,
		uint16_t size,
		uint8_t window, uint8_t disp_range, int32_t der_shift)
{
	int32_t c = 0, r = 0;
	uint32_t x = 0;
	uint32_t SAD_temp[2 * DISP_RANGE_MAX + 1]; // size must be at least 2*D + 1

	int32_t W = window;
	int32_t D = disp_range;


	uint8_t SHIFT_TOO_FAR = 0;
	memset(displacement, 0, size);

	int32_t border[2];

	if (der_shift < 0)
	{
		border[0] =  W + D + der_shift;
		border[1] = size - W - D;
	}
	else if(der_shift > 0)
	{
		border[0] =  W + D;
		border[1] = size - W - D - der_shift;
	}
	else
	{
		border[0] =  W + D;
		border[1] = size - W - D;
	}

	if(border[0] >= border[1] || abs(der_shift)>=10)
		SHIFT_TOO_FAR = 1;


	{
		// TODO: replace with arm offset subtract
		for (x = border[0]; x < border[1]; x++) {
			displacement[x] = 0;
			for (c = -D; c <= D; c++) {
				SAD_temp[c + D] = 0;
				for (r = -W; r <= W; r++) {
					SAD_temp[c + D] += abs(edge_histogram[x + r] - edge_histogram_prev[x + r + c + der_shift]);
				}
			}
			if(!SHIFT_TOO_FAR)
				displacement[x] = (int32_t)getMinimum(SAD_temp, 2 * D + 1) - D;
			else
				displacement[x]=0;
		}
	}

}

// Small supporting functions

uint32_t getMinimum(uint32_t *a, uint32_t n)
{
	uint32_t i;
	uint32_t min_ind = 0;
	uint32_t min_err = a[min_ind];
	uint32_t min_err_tot = 0;
	for (i = 1; i < n; i++) {
		if (a[i] <= min_err) {
			min_ind = i;
			min_err = a[i];
			min_err_tot += min_err;
		}
	}
	//*min_error = min_err_tot;
	return min_ind;
}

// Line_fit fits a line using least squares to the histogram disparity map
void line_fit(int32_t *displacement, int32_t *divergence, int32_t *flow, uint32_t size, uint32_t border,
		uint16_t RES)
{
	int32_t x;

	int32_t count = 0;
	int32_t sumY = 0;
	int32_t sumX = 0;
	int32_t sumX2 = 0;
	int32_t sumXY = 0;
	int32_t xMean = 0;
	int32_t yMean = 0;
	int32_t divergence_int = 0;
	int32_t border_int = (int32_t)border;
	int32_t size_int = (int32_t)size;
	uint32_t total_error = 0;

	*divergence = 0;
	*flow = 0;

	// compute fixed sums
	int32_t xend = size_int - border_int - 1;
	sumX = xend * (xend + 1) / 2 - border_int * (border_int + 1) / 2 + border_int;
	sumX2 = xend * (xend + 1) * (2 * xend + 1) / 6;
	xMean = (size_int - 1) / 2;
	count = size_int - 2 * border_int;

	for (x = border_int; x < size - border_int; x++) {
		sumY += displacement[x];
		sumXY += x * displacement[x];
	}

	yMean = RES * sumY / count;

	divergence_int = (RES * sumXY - sumX * yMean) / (sumX2 - sumX * xMean);    // compute slope of line ax + b
	*divergence = divergence_int;
	*flow = yMean - *divergence * xMean;  // compute b (or y) intercept of line ax + b

	for (x = border_int; x < size - border_int; x++) {
		total_error += abs(RES * displacement[x] - divergence_int * x + yMean);
	}

	//return total_error / size;
}
void draw_edgeflow_img(struct image_t *img, struct edge_flow_t edgeflow, struct edgeflow_displacement_t displacement, int32_t *edge_hist_x)
{
	struct point_t point1;
	struct point_t point2;
	struct point_t point1_prev;
	struct point_t point2_prev;
	struct point_t point1_extra;
	struct point_t point2_extra;
	uint16_t i;

	for(i = 120; i<240;i++)
	{
		point1.y = -(uint16_t)edge_hist_x[i]/100 + img->h/3;
		point1.x = i;
		point2.y = -(uint16_t)edge_hist_x[i+1]/100 + img->h/3;
		point2.x = i+1;

		point1_prev.y = -(uint16_t)displacement.horizontal[i]*5 + img->h*2/3;
		point1_prev.x = i;
		point2_prev.y = -(uint16_t)displacement.horizontal[i+1]*5 + img->h*2/3;
		point2_prev.x = i+1;

		image_draw_line(img, &point1,&point2);
		image_draw_line(img, &point1_prev,&point2_prev);
	}

	point1_extra.y = (edgeflow.horizontal_flow+edgeflow.horizontal_div * -180 )/ 100+ img->h/2;
	point1_extra.x = 0;
	point2_extra.y = (edgeflow.horizontal_flow+edgeflow.horizontal_div * 180 )/ 100 + img->h/2;
	point2_extra.x = 360;
	image_draw_line(img, &point1_extra,&point2_extra);
}
void edgeflow_calc_frame(struct opticflow_t *opticflow, struct opticflow_state_t *state, struct image_t *img,
		struct opticflow_result_t *result)
{
	// Define Static Variables
	static struct edge_hist_t edge_hist[MAX_HORIZON];
	static uint8_t current_frame_nr = 0;
	static struct edge_flow_t edgeflow;
	static uint8_t previous_frame_offset[2] = {1,1};

	// Define Normal variables
	struct edgeflow_displacement_t displacement;
	uint8_t disp_range = DISP_RANGE_MAX;
	uint16_t RES = 100;

	// Calculate current frame's edge histogram
	int32_t *edge_hist_x = edge_hist[current_frame_nr].horizontal;
	int32_t *edge_hist_y = edge_hist[current_frame_nr].vertical;
	calculate_edge_histogram(img, edge_hist_x, 'x',0);
	calculate_edge_histogram(img, edge_hist_y, 'y',0);

	// Copy frame time and angles of image to calculated edge histogram
	memcpy(&edge_hist[current_frame_nr].frame_time, &img->ts, sizeof(struct timeval));
	edge_hist[current_frame_nr].pitch = state->theta;
    edge_hist[current_frame_nr].roll = state->phi;

    // Adaptive Time Horizon:
    // if the flow measured in previous frame is small,
    // the algorithm will choose an frame further away back from the
    // current frame to detect subpixel flow

	if (MAX_HORIZON > 2) {

		uint32_t flow_mag_x, flow_mag_y;
		flow_mag_x = abs(edgeflow.horizontal_flow);
		flow_mag_y = abs(edgeflow.vertical_flow);
		uint32_t min_flow = 3;
		uint32_t max_flow = disp_range*RES - 3*RES;

		uint8_t previous_frame_offset_x = previous_frame_offset[0];
		uint8_t previous_frame_offset_y = previous_frame_offset[1];

		// IF statements which will decrement the previous frame offset
		// if the measured flow of last loop is higher than max value (higher flow measured)
		// and visa versa
		if (flow_mag_x > max_flow && previous_frame_offset_x > 1)
			previous_frame_offset[0] = previous_frame_offset_x - 1;
		if (flow_mag_x < min_flow && previous_frame_offset_x < MAX_HORIZON - 1)
			previous_frame_offset[0] = previous_frame_offset_x + 1;
		if (flow_mag_y > max_flow && previous_frame_offset_y > 1)
			previous_frame_offset[1] = previous_frame_offset_y - 1;
		if (flow_mag_y < min_flow && previous_frame_offset_y < MAX_HORIZON - 1)
			previous_frame_offset[1] = previous_frame_offset_y + 1;
	}

	//Wrap index previous frame offset from current frame nr.
	uint8_t previous_frame_x = (current_frame_nr - previous_frame_offset[0] + MAX_HORIZON) %
			MAX_HORIZON;
	uint8_t previous_frame_y = (current_frame_nr - previous_frame_offset[1] + MAX_HORIZON) %
			MAX_HORIZON;

	//Select edge histogram from the previous frame nr
	int32_t *prev_edge_histogram_x = edge_hist[previous_frame_x].horizontal;
	int32_t *prev_edge_histogram_y = edge_hist[previous_frame_y].vertical;

	//Calculate the corrosponding derotation of the two frames
 	int16_t der_shift_x = -(int16_t)((edge_hist[previous_frame_x].roll - edge_hist[current_frame_nr].roll) * (float)img->w / ( OPTICFLOW_FOV_W));
 	int16_t der_shift_y = -(int16_t)((edge_hist[previous_frame_x].pitch - edge_hist[current_frame_nr].pitch) * (float)img->h / ( OPTICFLOW_FOV_H));

    // Estimate pixel wise displacement of the edge histograms for x and y direction
	calculate_edge_displacement(edge_hist_x, prev_edge_histogram_x,
			displacement.horizontal, img->w,
			opticflow->window_size, disp_range,  der_shift_x);
	calculate_edge_displacement(edge_hist_y, prev_edge_histogram_y,
			displacement.vertical, img->h,
			opticflow->window_size, disp_range, der_shift_y);

	line_fit(displacement.horizontal, &edgeflow.horizontal_div,
			&edgeflow.horizontal_flow, img->w,
			opticflow->window_size + disp_range, RES);
	line_fit(displacement.vertical, &edgeflow.vertical_div,
			&edgeflow.vertical_flow, img->h,
			opticflow->window_size + disp_range, RES);

	result->flow_x = (int16_t)edgeflow.horizontal_flow/(previous_frame_offset[0]*RES);
	result->flow_y = (int16_t)edgeflow.vertical_flow/(previous_frame_offset[1]*RES);

	float fps_x = 0;
	float fps_y = 0;
	float time_diff_x = (float)(timeval_diff2(&edge_hist[previous_frame_x].frame_time, &img->ts)) / 1000.;
	float time_diff_y = (float)(timeval_diff2(&edge_hist[previous_frame_y].frame_time, &img->ts)) / 1000.;
	fps_x = 1/(time_diff_x);
	fps_y = 1/(time_diff_y);

	result->fps = fps_x;
	float vel_hor = edgeflow.horizontal_flow * fps_x* state->agl * OPTICFLOW_FOV_W / (img->w * RES);
	float vel_ver = edgeflow.vertical_flow * fps_y * state->agl * OPTICFLOW_FOV_H / (img->h * RES);

	result->vel_x = vel_ver;
	result->vel_y = - vel_hor;

#if OPTICFLOW_DEBUG && OPTICFLOW_SHOW_FLOW
	draw_edgeflow_img(img, edgeflow,displacement, *edge_hist_x)
#endif

	current_frame_nr = (current_frame_nr + 1) % MAX_HORIZON;


}


/**
 * Calculate the difference from start till finish
 * @param[in] *starttime The start time to calculate the difference from
 * @param[in] *finishtime The finish time to calculate the difference from
 * @return The difference in milliseconds
 */
static uint32_t timeval_diff2(struct timeval *starttime, struct timeval *finishtime)
{
	uint32_t msec;
	msec = (finishtime->tv_sec - starttime->tv_sec) * 1000;
	msec += (finishtime->tv_usec - starttime->tv_usec) / 1000;
	return msec;
}

