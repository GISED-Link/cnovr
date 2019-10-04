#include <cnovrtcc.h>
#include <cnovrparts.h>
#include <cnovrfocus.h>
#include <cnovr.h>
#include <cnovrutil.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>
#include <errno.h>

int handle;

cnovr_shader * shader;


/////////////////////////////////////////////////////////////////////////////////
// Display stuff

og_thread_t gtt;

int frame_in_buffer = -1;
int need_texture = 1;
XShmSegmentInfo shminfo;
Display * localdisplay;
int quitting;

#define MAX_DRAGGABLE_WINDOWS 16

struct DraggableWindow
{
	Window windowtrack;
	XImage * image;
	int width, height;
	cnovr_model * model;
	cnovr_texture * texture;
	cnovr_pose modelpose;
	cnovrfocus_capture focusblock;
};

struct DraggableWindow dwindows[MAX_DRAGGABLE_WINDOWS];
int current_window_check;


Window GetWindowIdBySubstring( const char * windownamematch );

int AllocateNewWindow( const char * name )
{
	Window wnd = GetWindowIdBySubstring( name );
	if( !wnd )
	{
		printf( "Can't find window name %s\n", name );
		return -1;
	}
	int i;
	struct DraggableWindow * dw;
	for( i = 0; i < MAX_DRAGGABLE_WINDOWS; i++ )
	{
		dw = &dwindows[i];
		if( !dw->windowtrack )
		{
			break;
		}
	}
	if( i == MAX_DRAGGABLE_WINDOWS )
	{
		printf( "Can't allocate another draggable window\n" );
		return -2;
	}

	dw->windowtrack = wnd;
	dw->width = 0;
	dw->height = 0;
	dw->image = 0;
	pose_make_identity( &dw->modelpose );

	printf( "SET INTERACTABLE: %p %p\n", dw->model, &dw->focusblock );
	CNOVRModelSetInteractable( dw->model, &dw->focusblock );

	return i;
}

int Xerrhandler(Display * d, XErrorEvent * e)
{
	printf( "XShmGetImage error\n" );
    return 0;
}


Window GetWindowIdBySubstring( const char * windownamematch )
{
	Window ret  = 0;

    Window rootWindow = RootWindow( localdisplay, DefaultScreen(localdisplay));
    Atom atom = XInternAtom(localdisplay, "_NET_CLIENT_LIST", true);
    Atom actualType;
    int format;
    unsigned long numItems;
    unsigned long bytesAfter;
    
    unsigned char *data = '\0';
    Window *list;    
    char *windowName;

    int status = XGetWindowProperty(localdisplay, rootWindow, atom, 0L, (~0L), false,
        AnyPropertyType, &actualType, &format, &numItems, &bytesAfter, &data);
    list = (Window *)data;
    
    if (status >= Success && numItems) {
        for (int i = 0; i < numItems; ++i) {
            status = XFetchName(localdisplay, list[i], &windowName);
            if (status >= Success && windowName ) {
				printf( "Seen window %s\n", windowName );
				if( strstr( windowName, windownamematch ) != 0 )
				{
					ret = list[i];
					break;
				}
            }
        }
    }
    XFree(windowName);
    XFree(data);

	return ret;
}

void * GetTextureThread( void * v )
{
	XInitThreads();
	localdisplay = XOpenDisplay(NULL);
	XSetErrorHandler(Xerrhandler);


	shminfo.shmid = shmget(IPC_PRIVATE,
		2048*4 * 2048,
		IPC_PRIVATE | IPC_EXCL | IPC_CREAT|0777);

	shminfo.shmaddr = shmat(shminfo.shmid, 0, 0);
	shminfo.readOnly = False;
	XShmAttach(localdisplay, &shminfo);


//	ListWindows();
	AllocateNewWindow( "Mozilla Firefox" );
	AllocateNewWindow( "Frame Timing" );
	AllocateNewWindow( " (" );

	while( !quitting )
	{
		if( !need_texture ) { OGUSleep( 2000 ); continue; }
		current_window_check = (current_window_check+1)%MAX_DRAGGABLE_WINDOWS;
		struct DraggableWindow * dw = &dwindows[current_window_check];
		if( !dw->windowtrack ) { if( current_window_check == 0 ) OGUSleep( 2000 ); continue; }

		XWindowAttributes attribs;
		XGetWindowAttributes(localdisplay, dw->windowtrack, &attribs);
		int taint = 0;
		if( attribs.width != dw->width ) taint = 1;
		if( attribs.height != dw->height ) taint = 1;
		int width = dw->width = attribs.width;
		int height = dw->height = attribs.height;

		if( taint  || !dw->image)
		{
			if( dw->image ) XDestroyImage( dw->image );
			dw->image = XShmCreateImage( localdisplay, 
				attribs.visual, attribs.depth,
				ZPixmap, NULL, &shminfo, width, height); 

			dw->image->data = shminfo.shmaddr;

			printf( "Gen Image: %p\n", dw->image );
		}
	 

	//	printf( "%d %d\n", dw->windowtrack, dw->image );
		double ds = OGGetAbsoluteTime();
		XShmGetImage(localdisplay, dw->windowtrack, dw->image, 0, 0, AllPlanes);
		double part1 = OGGetAbsoluteTime() - ds;
		//printf( "Got Frame %d %p %p %p\n", current_window_check, dw->image, dw->image->data, *(int32_t*)(&(dw->image->data[0])) );
		if( dw->image && dw->image->data && (void*)(&(dw->image->data[0])) != (void*)(-1) )
		{
			need_texture = 0;
			frame_in_buffer = current_window_check;
		}

		if( part1 > .005 )
			printf( "FRAME GET Took too long %10f\n", part1 );

		//No way we'd need to be woken up faster than this.
		OGUSleep( 2000 );
	}
	return 0;
}



void PreRender()
{
}

void Update()
{
}


void PostRender()
{
	if( frame_in_buffer >= 0 )
	{
		struct DraggableWindow * dw = &dwindows[frame_in_buffer];
		CNOVRTextureLoadDataNow( dw->texture, dw->width, dw->height, 4, 0, (void*)(&(dw->image->data[0])), 1 );
		need_texture = 1;
		frame_in_buffer = -1;
	}
}

void Render()
{
	int i;
	CNOVRRender( shader );
	for( i = 0; i < MAX_DRAGGABLE_WINDOWS; i++ )
	{
		struct DraggableWindow * dw = &dwindows[i];
		if( dw->windowtrack )
		{
			CNOVRRender( dw->texture );
			CNOVRRender( dw->model );
		}
	}
}


int DockableWindowFocusEvent( int event, cnovrfocus_capture * cap, cnovrfocus_properties * prop, int buttoninfo )
{
	CNOVRModelHandleFocusEvent( cap->opaque, prop, event, buttoninfo );
	return 0;
}



void prerender_startup( void * tag, void * opaquev )
{
	int i;
	for( i = 0; i < MAX_DRAGGABLE_WINDOWS; i++ )
	{
		struct DraggableWindow * dw = &dwindows[i];

		//XXX TODO: Wouldn't it be cool if we could make this a single render call?
		//Not sure how we would handle the textures, though.
		dw->model = CNOVRModelCreate( 0, 4, GL_TRIANGLES );
		cnovr_point4d extradata = { i, 0, 0, 0 };
		CNOVRModelAppendMesh( dw->model, 2, 2, 1, (cnovr_point3d){ 1, 1, 0 }, 0, &extradata );
		cnovr_pose offset = (cnovr_pose){ { 1, 0, 0, 0 }, { 1, 0, 0 }, 1 };
		CNOVRModelAppendMesh( dw->model, 2, 2, 1, (cnovr_point3d){ -1, 1, 0. }, &offset, &extradata );
		pose_make_identity( &dw->modelpose );
		dw->model->pose = &dw->modelpose;
		dw->focusblock.tag = 0;
		dw->focusblock.opaque = dw->model;
		dw->focusblock.cb = DockableWindowFocusEvent;
		dw->texture = CNOVRTextureCreate( 0, 0, 0 );
	}

	gtt = OGCreateThread( GetTextureThread, 0 );

	shader = CNOVRShaderCreate( "draggablewindow" );
	CNOVRListAdd( cnovrLRender, &handle, Render );
	CNOVRListAdd( cnovrLPrerender, &handle, PreRender );
	CNOVRListAdd( cnovrLUpdate, &handle, Update );
	CNOVRListAdd( cnovrLPostRender, &handle, PostRender );
}

void start( const char * identifier )
{


	printf( "Dockables start %s(%p)\n", identifier, identifier );
	CNOVRJobTack( cnovrQPrerender, prerender_startup, 0, 0, 0 );
	printf( "Dockables start OK %s\n", identifier );

}

void stop( const char * identifier )
{
	if( shminfo.shmid >= 0 )
	{
		shmdt(shminfo.shmaddr);
		/* 'remove' shared memory segment */
		shmctl(shminfo.shmid, IPC_RMID, NULL);
	}

	printf( "Stopping\n" );
	quitting = 1;
	if( localdisplay )
	{
		XCloseDisplay( localdisplay );
	}
	printf( "Joining\n" );
	if( gtt ) 
	{
		OGJoinThread( gtt );
	}
	printf( "Stopped\n" );

	CNOVRListDeleteTag( &handle );
	printf( "Dockables Destroying: %p %p\n" ,shader );
	int i;
	for( i = 0; i < MAX_DRAGGABLE_WINDOWS; i++ )
	{
		struct DraggableWindow * dw = &dwindows[i];
		CNOVRDelete( dw->model );
		CNOVRDelete( dw->texture );
	}

	CNOVRDelete( shader );
	printf( "Dockables End stop\n" );
}
