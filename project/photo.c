/*									tab:8
 *
 * photo.c - photo display functions
 *
 * "Copyright (c) 2011 by Steven S. Lumetta."
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE AUTHOR OR THE UNIVERSITY OF ILLINOIS BE LIABLE TO 
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL 
 * DAMAGES ARISING OUT  OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, 
 * EVEN IF THE AUTHOR AND/OR THE UNIVERSITY OF ILLINOIS HAS BEEN ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHOR AND THE UNIVERSITY OF ILLINOIS SPECIFICALLY DISCLAIM ANY 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE 
 * PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND NEITHER THE AUTHOR NOR
 * THE UNIVERSITY OF ILLINOIS HAS ANY OBLIGATION TO PROVIDE MAINTENANCE, 
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    3
 * Creation Date:   Fri Sep  9 21:44:10 2011
 * Filename:	    photo.c
 * History:
 *	SL	1	Fri Sep  9 21:44:10 2011
 *		First written (based on mazegame code).
 *	SL	2	Sun Sep 11 14:57:59 2011
 *		Completed initial implementation of functions.
 *	SL	3	Wed Sep 14 21:49:44 2011
 *		Cleaned up code for distribution.
 */


#include <string.h>

#include "assert.h"
#include "modex.h"
#include "photo.h"
#include "photo_headers.h"
#include "world.h"

/* bitmask to lower four bits */
#define LOWER_FOURBITS 0x0000000F
/* lv4 octree size */
#define LV4SIZE 4096
/* lv2 octree size */
#define LV2SIZE 64
/* pixel array size */
#define PAS 1050625


/* some constant */
const int lv4octree_size = 4096;	/* octreelv4 size */
const int lv2octree_size = 64;		/* octreelv2 size */	
const int pixel_array_size = 1025 * 1025;	/* maximum photo size */
const int lv4octree_chosen = 128;	/* chose 128 points */
const int twl = 12;			/* bit shift constant */
const int elev = 11;			/* bit shift constant */
const int svn = 7;			/* bit shift constant */
const int fv = 5;			/* bit shift constant */
const int ftn = 14;
const int nin = 9;
const int thr = 3;
const int palette_size = 192;	/* palette size */
const int orignal_offset = 64;	/* original offset of the palette */
const int bit3 = 0x3;
const int bit6 = 0x3F;	/* bitmask of the lowest 6 bits */
const int bit5 = 0x1F;	/* 5 bits bitmask */

/* types local to this file (declared in types.h) */

/* 
 * A room photo.  Note that you must write the code that selects the
 * optimized palette colors and fills in the pixel data using them as 
 * well as the code that sets up the VGA to make use of these colors.
 * Pixel data are stored as one-byte values starting from the upper
 * left and traversing the top row before returning to the left of
 * the second row, and so forth.  No padding should be used.
 */
struct photo_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t        palette[192][3];     /* optimized palette colors */
    uint8_t*       img;                 /* pixel data               */
};

/* 
 * An object image.  The code for managing these images has been given
 * to you.  The data are simply loaded from a file, where they have 
 * been stored as 2:2:2-bit RGB values (one byte each), including 
 * transparent pixels (value OBJ_CLR_TRANSP).  As with the room photos, 
 * pixel data are stored as one-byte values starting from the upper 
 * left and traversing the top row before returning to the left of the 
 * second row, and so forth.  No padding is used.
 */
struct image_t {
    photo_header_t hdr;			/* defines height and width */
    uint8_t*       img;                 /* pixel data               */
};

/* An object lv4octree. This data structure provides the 
 * data of the 4096 (16*16*16) format. 
 * It contains several fields:
 * color_r (the first 4 bit color of the whole 8 bit color lv4octree); color_g; color_b
 * avg_r,abg_g,avg_b (the last 4 bit color of the whole 8 bit color lv4octree)
 * num: The number of the nodes belong to this node.
 */
typedef struct{
	
	uint32_t color_rgb;

	uint32_t avg_r;			/* the 6 bits of the red value of the color */
	uint32_t avg_g;			/* the 6 bits of the green value of the color */
	uint32_t avg_b;			/* the 6 bits of the blue value of the color */

	int num;				/* the number of nodes which belong to this lv4octree node */
}Oct_Tree;

Oct_Tree lv4octree[LV4SIZE];	/* we have level 4 octree with 4096 points */
Oct_Tree lv2octree[LV2SIZE];	/* we have level 2 octree with 64 points */

int8_t is128[LV4SIZE];	/* Identify which pixel is the first 128 bit */

uint16_t pixel_array[PAS]; /* Document all of the pixel in the image */

/* file-scope variables */

/* 
 * The room currently shown on the screen.  This value is not known to 
 * the mode X code, but is needed when filling buffers in callbacks from 
 * that code (fill_horiz_buffer/fill_vert_buffer).  The value is set 
 * by calling prep_room.
 */
static const room_t* cur_room = NULL; 


/* 
 * fill_horiz_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the leftmost 
 *                pixel of a line to be drawn on the screen, this routine 
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- leftmost pixel of line to be drawn 
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_horiz_buffer (int x, int y, unsigned char buf[SCROLL_X_DIM])
{
    int            idx;   /* loop index over pixels in the line          */ 
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgx;  /* loop index over pixels in object image      */ 
    int            yoff;  /* y offset into object image                  */ 
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_X_DIM; idx++) {
        buf[idx] = (0 <= x + idx && view->hdr.width > x + idx ?
		    view->img[view->hdr.width * y + x + idx] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (y < obj_y || y >= obj_y + img->hdr.height ||
	    x + SCROLL_X_DIM <= obj_x || x >= obj_x + img->hdr.width) {
	    continue;
	}

	/* The y offset of drawing is fixed. */
	yoff = (y - obj_y) * img->hdr.width;

	/* 
	 * The x offsets depend on whether the object starts to the left
	 * or to the right of the starting point for the line being drawn.
	 */
	if (x <= obj_x) {
	    idx = obj_x - x;
	    imgx = 0;
	} else {
	    idx = 0;
	    imgx = x - obj_x;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_X_DIM > idx && img->hdr.width > imgx; idx++, imgx++) {
	    pixel = img->img[yoff + imgx];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/* 
 * fill_vert_buffer
 *   DESCRIPTION: Given the (x,y) map pixel coordinate of the top pixel of 
 *                a vertical line to be drawn on the screen, this routine 
 *                produces an image of the line.  Each pixel on the line
 *                is represented as a single byte in the image.
 *
 *                Note that this routine draws both the room photo and
 *                the objects in the room.
 *
 *   INPUTS: (x,y) -- top pixel of line to be drawn 
 *   OUTPUTS: buf -- buffer holding image data for the line
 *   RETURN VALUE: none
 *   SIDE EFFECTS: none
 */
void
fill_vert_buffer (int x, int y, unsigned char buf[SCROLL_Y_DIM])
{
    int            idx;   /* loop index over pixels in the line          */ 
    object_t*      obj;   /* loop index over objects in the current room */
    int            imgy;  /* loop index over pixels in object image      */ 
    int            xoff;  /* x offset into object image                  */ 
    uint8_t        pixel; /* pixel from object image                     */
    const photo_t* view;  /* room photo                                  */
    int32_t        obj_x; /* object x position                           */
    int32_t        obj_y; /* object y position                           */
    const image_t* img;   /* object image                                */

    /* Get pointer to current photo of current room. */
    view = room_photo (cur_room);

    /* Loop over pixels in line. */
    for (idx = 0; idx < SCROLL_Y_DIM; idx++) {
        buf[idx] = (0 <= y + idx && view->hdr.height > y + idx ?
		    view->img[view->hdr.width * (y + idx) + x] : 0);
    }

    /* Loop over objects in the current room. */
    for (obj = room_contents_iterate (cur_room); NULL != obj;
    	 obj = obj_next (obj)) {
	obj_x = obj_get_x (obj);
	obj_y = obj_get_y (obj);
	img = obj_image (obj);

        /* Is object outside of the line we're drawing? */
	if (x < obj_x || x >= obj_x + img->hdr.width ||
	    y + SCROLL_Y_DIM <= obj_y || y >= obj_y + img->hdr.height) {
	    continue;
	}

	/* The x offset of drawing is fixed. */
	xoff = x - obj_x;

	/* 
	 * The y offsets depend on whether the object starts below or 
	 * above the starting point for the line being drawn.
	 */
	if (y <= obj_y) {
	    idx = obj_y - y;
	    imgy = 0;
	} else {
	    idx = 0;
	    imgy = y - obj_y;
	}

	/* Copy the object's pixel data. */
	for (; SCROLL_Y_DIM > idx && img->hdr.height > imgy; idx++, imgy++) {
	    pixel = img->img[xoff + img->hdr.width * imgy];

	    /* Don't copy transparent pixels. */
	    if (OBJ_CLR_TRANSP != pixel) {
		buf[idx] = pixel;
	    }
	}
    }
}


/* 
 * image_height
 *   DESCRIPTION: Get height of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
image_height (const image_t* im)
{
    return im->hdr.height;
}


/* 
 * image_width
 *   DESCRIPTION: Get width of object image in pixels.
 *   INPUTS: im -- object image pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of object image im in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
image_width (const image_t* im)
{
    return im->hdr.width;
}

/* 
 * photo_height
 *   DESCRIPTION: Get height of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: height of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
photo_height (const photo_t* p)
{
    return p->hdr.height;
}


/* 
 * photo_width
 *   DESCRIPTION: Get width of room photo in pixels.
 *   INPUTS: p -- room photo pointer
 *   OUTPUTS: none
 *   RETURN VALUE: width of room photo p in pixels
 *   SIDE EFFECTS: none
 */
uint32_t 
photo_width (const photo_t* p)
{
    return p->hdr.width;
}


/* 
 * prep_room
 *   DESCRIPTION: Prepare a new room for display.  You might want to set
 *                up the VGA palette registers according to the color
 *                palette that you chose for this room.
 *   INPUTS: r -- pointer to the new room
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: changes recorded cur_room for this file
 */
void
prep_room (const room_t* r)
{
    /* Record the current room. */
	photo_t *pptr = room_photo(r);
	fill_my_palette(pptr->palette);
    cur_room = r;
}


/* 
 * read_obj_image
 *   DESCRIPTION: Read size and pixel data in 2:2:2 RGB format from a
 *                photo file and create an image structure from it.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the image
 */
image_t*
read_obj_image (const char* fname)
{
    FILE*    in;		/* input file               */
    image_t* img = NULL;	/* image structure          */
    uint16_t x;			/* index over image columns */
    uint16_t y;			/* index over image rows    */
    uint8_t  pixel;		/* one pixel from the file  */

    /* 
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the image pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (img = malloc (sizeof (*img))) ||
	NULL != (img->img = NULL) || /* false clause for initialization */
	1 != fread (&img->hdr, sizeof (img->hdr), 1, in) ||
	MAX_OBJECT_WIDTH < img->hdr.width ||
	MAX_OBJECT_HEIGHT < img->hdr.height ||
	NULL == (img->img = malloc 
		 (img->hdr.width * img->hdr.height * sizeof (img->img[0])))) {
	if (NULL != img) {
	    if (NULL != img->img) {
	        free (img->img);
	    }
	    free (img);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }

    /* 
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = img->hdr.height; y-- > 0; ) {

	/* Loop over columns from left to right. */
	for (x = 0; img->hdr.width > x; x++) {

	    /* 
	     * Try to read one 8-bit pixel.  On failure, clean up and 
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (img->img);
		free (img);
	        (void)fclose (in);
		return NULL;
	    }

	    /* Store the pixel in the image data. */
	    img->img[img->hdr.width * y + x] = pixel;
	}
    }

    /* All done.  Return success. */
    (void)fclose (in);
    return img;
}

/*
 * lv4octree_init
 *   DESCRIPTION: Initialization of the level 4 of the octree.
 *  			Giving the initial value of all field in the structure
 *  			To make preparation for the following opeartion
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: none
 *   SIDE EFFECTS: modify the value in lv4octree & init the lv2octree
 */
void
lv4octree_init(){
	int i = 0;
	for(; i <= (lv4octree_size - 1); i++){

		lv4octree[i].color_rgb = i;	/* set the overall color of the node */

		lv4octree[i].avg_r = 0;	/* Initialize the value of the red component of the average color */
		lv4octree[i].avg_g = 0;	/* Initialize the value of the green component of the average color */
		lv4octree[i].avg_b = 0; /* Initialize the value of the blue component of the average color */
		
		lv4octree[i].num = 0;	/* Initialize the number of pixels belong to the node of the lv4octree */

		/* Second Part, init the is128 function */
		is128[i] = 0;
	}
	/* SIDE EFFECTS: init lv2octree */
	for(i = 0; i < lv2octree_size; i ++){
		lv2octree[i].avg_r = 0;	/* set the average to zero */
		lv2octree[i].avg_g = 0;	/* set the average to zero */
		lv2octree[i].avg_b = 0;
		lv2octree[i].num = 0;	/* set the num of the pixels that belong to it to be zero */
	}
	return ;
}

/*
 * lv4octree_cmp
 *   DESCRIPTION: Helper function to assist the sorting of lv4octree
 *   INPUTS: two different pointer of the element in lv4octree
 *   OUTPUTS: 0 (if first element > second element), 1 (if first element < second element)
 *   RETURN VALUE: 0 or 1
 *   SIDE EFFECTS: NONE
 */
int
lv4octree_cmp(const void* a, const void* b){
	Oct_Tree* ele1 = (Oct_Tree*) a;
	Oct_Tree* ele2 = (Oct_Tree*) b;
	/* In order to sort it from the bigger to lower */
	return (ele1->num) < (ele2->num) ;
}

/* 
 * read_photo
 *   DESCRIPTION: Read size and pixel data in 5:6:5 RGB format from a
 *                photo file and create a photo structure from it.
 *                Code provided simply maps to 2:2:2 RGB.  You must
 *                replace this code with palette color selection, and
 *                must map the image pixels into the palette colors that
 *                you have defined.
 *   INPUTS: fname -- file name for input
 *   OUTPUTS: none
 *   RETURN VALUE: pointer to newly allocated photo on success, or NULL
 *                 on failure
 *   SIDE EFFECTS: dynamically allocates memory for the photo
 */
photo_t*
read_photo (const char* fname)
{
    FILE*    in;	/* input file               */
    photo_t* p = NULL;	/* photo structure          */
    uint16_t x;		/* index over image columns */
    uint16_t y;		/* index over image rows    */
    uint16_t pixel;	/* one pixel from the file  */

    /* 
     * Open the file, allocate the structure, read the header, do some
     * sanity checks on it, and allocate space to hold the photo pixels.
     * If anything fails, clean up as necessary and return NULL.
     */
    if (NULL == (in = fopen (fname, "r+b")) ||
	NULL == (p = malloc (sizeof (*p))) ||
	NULL != (p->img = NULL) || /* false clause for initialization */
	1 != fread (&p->hdr, sizeof (p->hdr), 1, in) ||
	MAX_PHOTO_WIDTH < p->hdr.width ||
	MAX_PHOTO_HEIGHT < p->hdr.height ||
	NULL == (p->img = malloc 
		 (p->hdr.width * p->hdr.height * sizeof (p->img[0])))) {
	if (NULL != p) {
	    if (NULL != p->img) {
	        free (p->img);
	    }
	    free (p);
	}
	if (NULL != in) {
	    (void)fclose (in);
	}
	return NULL;
    }
	/* CRITICAL SECTION ABOUT MY CODE ABOUT MP2 CHECKPOINT 2 */

	/* INITIALIZATION ABOUT MY OWN OCTREE OF LVL 4 */
	lv4octree_init();
	int tot = 0; /* Record the pixel */
    /* 
     * Loop over rows from bottom to top.  Note that the file is stored
     * in this order, whereas in memory we store the data in the reverse
     * order (top to bottom).
     */
    for (y = p->hdr.height; y-- > 0; ) {

	/* Loop over columns from left to right. */
	for (x = 0; p->hdr.width > x; x++) {

	    /* 
	     * Try to read one 16-bit pixel.  On failure, clean up and 
	     * return NULL.
	     */
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (p->img);
		free (p);
	        (void)fclose (in);
		return NULL;

	    }

/*---------------------------------Origin Code------------------------------*/
	    /* 
	     * 16-bit pixel is coded as 5:6:5 RGB (5 bits red, 6 bits green,
	     * and 6 bits blue).  We change to 2:2:2, which we've set for the
	     * game objects.  You need to use the other 192 palette colors
	     * to specialize the appearance of each photo.
	     *
	     * In this code, you need to calculate the p->palette values,
	     * which encode 6-bit RGB as arrays of three uint8_t's.  When
	     * the game puts up a photo, you should then change the palette 
	     * to match the colors needed for that photo.
	     */
	    /* p->img[p->hdr.width * y + x] = (((pixel >> 14) << 4) |
					    (((pixel >> 9) & 0x3) << 2) |
					    ((pixel >> 3) & 0x3));*/
/*---------------------------------------------------------------------------*/
		
		/* Octree Section */
		/* Actually, it is only the first iteration of the loop */
		/* Therefore, the first iteration's mission is to build the octree of lv4 */
		
		/* First step, calculate the offset of the pixel of the octree */
		uint32_t pixel_r = pixel;
		uint32_t pixel_g = pixel;
		uint32_t pixel_b = pixel;
		/* the origin data is pixel (565 RGB) */
		pixel_array[tot] = pixel;
		tot += 1;
		/* Obtain the higher 5 bit R component and convert it to 4 bits */
		pixel_r >>= twl;	/* Get higher 4 bit of the pixel */
		pixel_r &= LOWER_FOURBITS;

		/* Obtain the higher 4 bit G component and convert it to 4 bit */
		pixel_g >>= svn;
		pixel_g &= LOWER_FOURBITS;	/* eliminate all higher bits, only choose the lower 4 bits */

		/* Obtain the higher 5 bit B component and convert it to 4 bits */
		pixel_b >>= 1;
		pixel_b &= LOWER_FOURBITS;

		/* Then aggrate it into 12 bit */
		uint32_t pixel_rgb = ( (pixel_r << 8) | (pixel_g << 4) | (pixel_b) ); /* Offset of the rgb value */
		
		/* Then add the color to the red green and blue component to the lv4octree */
		lv4octree[pixel_rgb].avg_r += ((pixel >> 11) << 1); /* Because it has only five bits, we need to shift right */
		lv4octree[pixel_rgb].avg_g += ((pixel >> 5) & bit6); /* Green component has six bits, and we should eliminate all higher bits */
		lv4octree[pixel_rgb].avg_b += ((pixel & 0x1F) << 1); /* Blue component has only five bits, we should first eliminate all higher bits and shift left for 1 bits */
		lv4octree[pixel_rgb].num += 1; /* Once one point is confined in this node, I add it to the node */ 
	}
    }

	/* After doing it, we have known that the octree, then we should sort it */
	/* Use std qsort and define my own compare function */
	qsort(lv4octree,lv4octree_size,sizeof(lv4octree[0]),lv4octree_cmp);

	/* set the first 128 element in lv4 */
	int i;
	for(i = 0; i < lv4octree_chosen; i++){
		is128[lv4octree[i].color_rgb] = orignal_offset + i; /* give the first 128 element the index in the FINAL buffer */
	}
	/* If there has point which belongs to the lv4node, calculate the average (only first 128 elements) */
	for(i = 0; i < lv4octree_chosen; i++){
		if(lv4octree[i].num != 0){
			lv4octree[i].avg_r /= lv4octree[i].num;
			lv4octree[i].avg_g /= lv4octree[i].num;
			lv4octree[i].avg_b /= lv4octree[i].num;
		}
		/* Set the first 128 Palette */
		p->palette[i][0] = (uint8_t) lv4octree[i].avg_r;
		p->palette[i][1] = (uint8_t) lv4octree[i].avg_g;
		p->palette[i][2] = (uint8_t) lv4octree[i].avg_b;
	}
	
	tot = 0;
	/* Second Iteration: find the node which belongs to the lv4 octree and set the rest of the node to the lv2octree */
    for (y = p->hdr.height; y-- > 0; ) {

	/* Loop over columns from left to right. */
	for (x = 0; p->hdr.width > x; x++) {

	    /* 
	     * Try to read one 16-bit pixel.  On failure, clean up and 
	     * return NULL.
	     */
		/*
	    if (1 != fread (&pixel, sizeof (pixel), 1, in)) {
		free (p->img);
		free (p);
	        (void)fclose (in);
		return NULL;
	    }*/

		/* Octree Section */
		/* Actually, it is only the first iteration of the loop */
		/* Therefore, the first iteration's mission is to build the octree of lv4 */
		
		/* First step, calculate the offset of the pixel of the octree */
		uint32_t pixel_r = pixel_array[tot];
		uint32_t pixel_g = pixel_array[tot];
		uint32_t pixel_b = pixel_array[tot];
		pixel = pixel_array[tot];
		/* the origin data is pixel (565 RGB) */
		tot += 1;
		/* Obtain the higher 5 bit R component and convert it to 4 bits */
		pixel_r >>= twl;	/* Get higher 4 bit of the pixel */
		pixel_r &= LOWER_FOURBITS;

		/* Obtain the higher 4 bit G component and convert it to 4 bit */
		pixel_g >>= svn;
		pixel_g &= LOWER_FOURBITS;	/* eliminate all higher bits, only choose the lower 4 bits */

		/* Obtain the higher 5 bit B component and convert it to 4 bits */
		pixel_b >>= 1;
		pixel_b &= LOWER_FOURBITS;

		/* Then aggrate it into 12 bit */
		uint32_t pixel_rgb = ( (pixel_r << 8) | (pixel_g << 4) | (pixel_b) ); /* Offset of the rgb value */
		/* if it belongs to the first 128 element */
		if(is128[pixel_rgb] != 0){
			/* It should be set to the pointer */
			p->img[p->hdr.width * y + x] = is128[pixel_rgb];
		}else{
			/* It belongs to the lvl2octree */
			uint32_t lv2octree_pixel_r = ((pixel >> ftn) & bit3); /* Get the higher two bits */
			uint32_t lv2octree_pixel_g = ((pixel >> nin) & bit3); /* Get the higher two bits of the green */
			uint32_t lv2octree_pixel_b = ((pixel >> thr) & bit3); /* Get the higher two bits of the blue */
			uint32_t lv2octree_pixel_rgb = ((lv2octree_pixel_r << 4) | (lv2octree_pixel_g << 2) | (lv2octree_pixel_b));

			/* Update the data in the lv2octree */
			/* Then add the color to the red green and blue component to the lv4octree */
			lv2octree[lv2octree_pixel_rgb].avg_r += ((pixel >> elev) << 1); /* Because it has only five bits, we need to shift right */
			lv2octree[lv2octree_pixel_rgb].avg_g += ((pixel >> fv) & bit6); /* Green component has six bits, and we should eliminate all higher bits */
			lv2octree[lv2octree_pixel_rgb].avg_b += ((pixel & bit5) << 1); /* Blue component has only five bits, we should first eliminate all higher bits and shift left for 1 bits */
			lv2octree[lv2octree_pixel_rgb].num += 1; /* Once one point is confined in this node, I add it to the node */  

			/* Finally, set the pointer to the real offset */
			p->img[p->hdr.width * y + x] = orignal_offset + lv4octree_chosen + lv2octree_pixel_rgb;
		}
	}
	}
	/* Write the value into the palette */
	int j ;
	for(j = lv4octree_chosen; j < palette_size; j++){
		if(lv2octree[j - lv4octree_chosen].num == 0)	/* because we need to calculate the average, so I igonre the value of the palette */
			continue;
		p->palette[j][0] = (uint8_t)(lv2octree[j - lv4octree_chosen].avg_r / lv2octree[j - lv4octree_chosen].num);	/* tick it into palette */
		p->palette[j][1] = (uint8_t)(lv2octree[j - lv4octree_chosen].avg_g / lv2octree[j - lv4octree_chosen].num);
		p->palette[j][2] = (uint8_t)(lv2octree[j - lv4octree_chosen].avg_b / lv2octree[j - lv4octree_chosen].num);
	}

	/* CRITICAL SECTION ABOUT MY CODE ABOUT MP2 CHECKPOINT 2 */
 
    /* All done.  Return success. */
    (void)fclose (in);
    return p;
}


