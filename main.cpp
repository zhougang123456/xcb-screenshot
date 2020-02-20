#include <QCoreApplication>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/xcb_aux.h>
#include <pixman.h>

typedef unsigned char  BYTE;
typedef unsigned short	WORD;
typedef unsigned int  DWORD;

#define PACKED __attribute__(( packed, aligned(2)))

typedef struct tagBITMAPFILEHEADER{
     WORD     bfType;        //Linux此值为固定值，0x4d42
     DWORD    bfSize;        //BMP文件的大小，包含三部分
     WORD     bfReserved1;    //置0
     WORD     bfReserved2;
     DWORD    bfOffBits;     //文件起始位置到图像像素数据的字节偏移量

}PACKED BITMAPFILEHEADER;


typedef struct tagBITMAPINFOHEADER{
     DWORD    biSize;          //文件信息头的大小，40
     DWORD    biWidth;         //图像宽度
     DWORD    biHeight;        //图像高度
     WORD     biPlanes;        //BMP存储RGB数据，总为1
     WORD     biBitCount;      //图像像素位数，笔者RGB位数使用24
     DWORD    biCompression;   //压缩 0：不压缩  1：RLE8 2：RLE4
     DWORD    biSizeImage;     //4字节对齐的图像数据大小
     DWORD    biXPelsPerMeter; //水平分辨率  像素/米
     DWORD    biYPelsPerMeter;  //垂直分辨率  像素/米
     DWORD    biClrUsed;        //实际使用的调色板索引数，0：使用所有的调色板索引
     DWORD    biClrImportant;
}BITMAPINFOHEADER;

static void dump_bmp(char* buffer, int width, int height)
{
    static int id = 0;
    char file[200];
    if (id > 50){
        return;
    }
    printf("creating bmp!\n");
    sprintf(file, "/home/zhougang/xcb/%d.bmp", id++);

    FILE* f = fopen(file, "wb");
    if (!f) {
       printf("Error creating bmp!\n");
       return;
    }

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = 0;
    bi.biSizeImage = width*height*4;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    BITMAPFILEHEADER bf;
    bf.bfType = 0x4d42;
    bf.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bi.biSizeImage;
    bf.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf.bfReserved1 = 0;
    bf.bfReserved2 = 0;

    fwrite(&bf,sizeof(BITMAPFILEHEADER),1,f);                      //写入文件头
    fwrite(&bi,sizeof(BITMAPINFOHEADER),1,f);                      //写入信息头
    fwrite(buffer,bi.biSizeImage,1,f);

    printf("width : %d height: %d\n", width, height);
    fclose(f);

}
static xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen)
{
    xcb_screen_iterator_t iter;

    iter = xcb_setup_roots_iterator(xcb_get_setup(c));
    for (; iter.rem; --screen, xcb_screen_next(&iter))
        if (screen == 0)
            return iter.data;

    return NULL;
}

static int register_for_events(xcb_connection_t *conn, xcb_window_t root)
{
    uint32_t events = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    cookie = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, &events);
    error = xcb_request_check(conn, cookie);
    if (error) {
        fprintf(stderr,
                "Error:  Could not register normal events; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return 0;
    }
    return 0;
}

static void handle_cursor_notify(xcb_connection_t *conn,
                                 xcb_xfixes_cursor_notify_event_t *cev)
{
     xcb_xfixes_get_cursor_image_cookie_t icookie;
     xcb_xfixes_get_cursor_image_reply_t *ir;
     xcb_generic_error_t *error;
     int imglen;
     uint32_t *imgdata;
     icookie = xcb_xfixes_get_cursor_image(conn);
     ir = xcb_xfixes_get_cursor_image_reply(conn, icookie, &error);
     if (error) {
        printf("Could not get cursor_image_reply; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
         return;
     }
     if (!ir){
         return;
     }
     imglen = xcb_xfixes_get_cursor_image_cursor_image_length(ir);
     imgdata = xcb_xfixes_get_cursor_image_cursor_image(ir);
     printf("cursor x: %d y: %d width: %d height: %d x_hot_spot: %d y_hot_spot: %d len: %d",
            ir->x, ir->y, ir->width, ir->height, ir->xhot, ir->yhot, imglen);
     //dump_bmp((char*)imgdata, ir->width, ir->height);
     free(ir);
}

static void handle_damage_notify(xcb_connection_t *conn,
                                 xcb_damage_damage_t damage,
                                 xcb_damage_notify_event_t *dev,
                                 pixman_region16_t * damage_region)
{
     int i, n;
     pixman_box16_t *p;
     xcb_get_image_cookie_t cookie;
     xcb_generic_error_t *e;
     xcb_get_image_reply_t *reply;

     pixman_region_union_rect(damage_region, damage_region,
                         dev->area.x, dev->area.y, dev->area.width, dev->area.height);
     if (dev->level & 0x80){
         return;
     }
     xcb_damage_subtract(conn, damage,
                    XCB_XFIXES_REGION_NONE, XCB_XFIXES_REGION_NONE);
     p = pixman_region_rectangles(damage_region, &n);
     for (i = 0; i < n; i++){
         //printf("dirty rect count: %d: x: %d y: %d width: %d height: %d\n",
         //          n, p[i].x1, p[i].y1, p[i].x2 - p[i].x1, p[i].y2 - p[i].y1);

         cookie = xcb_get_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, dev->drawable,
                                p[i].x1, p[i].y1, p[i].x2 - p[i].x1, p[i].y2 - p[i].y1, ~0);
         reply = xcb_get_image_reply(conn, cookie, &e);
         if (e) {
            printf("xcb_shm_get_image from failed!\n");
            return;
         }
         uint8_t * data = xcb_get_image_data(reply);
         printf("data len: %d depth: %d image len: %d\n",
                reply->length, reply->depth, xcb_get_image_data_length(reply));

         dump_bmp((char*)data, p[i].x2 - p[i].x1, p[i].y2 - p[i].y1);
     }
     pixman_region_clear(damage_region);
}
static void handle_configure_notify(xcb_window_t root,
                                    xcb_configure_notify_event_t *cev)
{
     if (cev->window != root) {
         printf("not main window; skipping.\n");
         return;
     }
     printf("resolution changed to width: %d height: %d", cev->width, cev->height);
 }

int main(int argc, char *argv[])
{
    int src;
    int rc;
    xcb_damage_query_version_cookie_t dcookie;
    xcb_damage_query_version_reply_t *damage_version;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t *error;
    xcb_screen_t *screen;
    xcb_window_t root;
    const xcb_query_extension_reply_t *damage_ext;
    const xcb_query_extension_reply_t *xfixes_ext;

    xcb_connection_t *conn = xcb_connect(NULL, &src);
    printf("screen id: %d \n", src);
    screen = screen_of_display(conn, src);
    root = screen->root;
    printf("screen width: %d, height %d \n", screen->width_in_pixels, screen->height_in_pixels);

    damage_ext = xcb_get_extension_data(conn, &xcb_damage_id);
    if (!damage_ext) {
        fprintf(stderr, "Error:  XDAMAGE not found on display\n");
        return 0;
    }

    dcookie = xcb_damage_query_version(conn, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
    damage_version = xcb_damage_query_version_reply(conn, dcookie, &error);
    if (error) {
        fprintf(stderr, "Error:  Could not query damage; type %d; code %d; major %d; minor %d\n",
        error->response_type, error->error_code, error->major_code, error->minor_code);
        return 0;
    }
    free(damage_version);

    xcb_damage_damage_t damage = xcb_generate_id(conn);
    cookie = xcb_damage_create_checked(conn, damage, root, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
    error = xcb_request_check(conn, cookie);
    if (error) {
       fprintf(stderr, "Error:  Could not create damage; type %d; code %d; major %d; minor %d\n",
               error->response_type, error->error_code, error->major_code, error->minor_code);
       return 0;
    }

    xfixes_ext = xcb_get_extension_data(conn, &xcb_xfixes_id);
    if (!xfixes_ext) {
        fprintf(stderr, "Error:  XFIXES not found on display\n");
        return 0;
    }
    xcb_xfixes_query_version(conn, XCB_XFIXES_MAJOR_VERSION, XCB_XFIXES_MINOR_VERSION);

    cookie = xcb_xfixes_select_cursor_input_checked(conn, root,
                                                   XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
    error = xcb_request_check(conn, cookie);
    if (error) {
        fprintf(stderr,
                "Error:  Could not select cursor input; type %d; code %d; major %d; minor %d\n",
                error->response_type, error->error_code, error->major_code, error->minor_code);
        return 0;
    }

    rc = register_for_events(conn, root);

    xcb_generic_event_t *ev = NULL;
    pixman_region16_t damage_region;
    pixman_region_init(&damage_region);
    while ((ev = xcb_wait_for_event(conn))) {
        if (ev->response_type == xfixes_ext->first_event + XCB_XFIXES_CURSOR_NOTIFY){
            printf("cursor notity!\n");
            handle_cursor_notify(conn, (xcb_xfixes_cursor_notify_event_t *) ev);
        }
        else if (ev->response_type == damage_ext->first_event + XCB_DAMAGE_NOTIFY){
            //printf("damage notity!\n");
            //handle_damage_notify(conn, damage, (xcb_damage_notify_event_t *) ev, &damage_region);
        }
        else if (ev->response_type == XCB_CONFIGURE_NOTIFY){
            printf("configure notity!\n");
            handle_configure_notify(root, (xcb_configure_notify_event_t *) ev);
        }
        else{
            printf("Unexpected X event %d", ev->response_type);
        }

        free(ev);
    }
    while ((ev = xcb_poll_for_event(conn))){
        free(ev);
    }
    pixman_region_clear(&damage_region);

    return 0;
}
