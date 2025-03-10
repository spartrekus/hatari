#include "libretro.h"

#include "libretro-hatari.h"

#include "STkeymap.h"

#include "retro_strings.h"
#include "retro_files.h"
#include "retro_disk_control.h"
static dc_storage* dc;

// LOG
retro_log_printf_t log_cb;

cothread_t mainThread;
cothread_t emuThread;

int CROP_WIDTH;
int CROP_HEIGHT;
int VIRTUAL_WIDTH ;
int retrow=1024; 
int retroh=1024;

extern unsigned short int bmp[1024*1024];
extern int STATUTON,SHOWKEY,SHIFTON,pauseg,SND ,snd_sampler;
extern short signed int SNDBUF[1024*2];
extern char RPATH[512];
extern char RETRO_DIR[512];
extern char RETRO_TOS[512];
extern struct retro_midi_interface *MidiRetroInterface;

#include "cmdline.c"

extern void update_input(void);
extern void texture_init(void);
extern void texture_uninit(void);
extern void Emu_init();
extern void Emu_uninit();

const char *retro_save_directory;
const char *retro_system_directory;
const char *retro_content_directory;

static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;

static struct retro_input_descriptor input_descriptors[] = {
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Fire" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Enter GUI" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Mouse mode toggle" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Keyboard overlay" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Toggle m/k status" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Joystick number" },
   { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Mouse speed" },
   // Terminate
   { 255, 255, 255, 255, NULL }
};

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   struct retro_variable variables[] = {
      {
         "Hatari_resolution",
         "Internal resolution; 640x480|832x576|832x588|800x600|960x720|1024x768|1024x1024",

      },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
}


static void update_variables(void)
{
   struct retro_variable var = {
      .key = "Hatari_resolution",
   };

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      char *pch;
      char str[100];
	  snprintf(str, sizeof(str), "%s", var.value);

      pch = strtok(str, "x");
      if (pch)
         retrow = strtoul(pch, NULL, 0);
      pch = strtok(NULL, "x");
      if (pch)
         retroh = strtoul(pch, NULL, 0);

      fprintf(stderr, "[libretro-test]: Got size: %u x %u.\n", retrow, retroh);

      CROP_WIDTH =retrow;
      CROP_HEIGHT= (retroh-80);
      VIRTUAL_WIDTH = retrow;
      texture_init();
      //reset_screen();
   }
}

static void retro_wrap_emulator()
{
   pre_main(RPATH);

   pauseg=-1;

   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, 0); 

   // Were done here
   co_switch(mainThread);

   // Dead emulator, but libco says not to return
   while(true)
   {
      LOGI("Running a dead emulator.");
      co_switch(mainThread);
   }
}

void Emu_init()
{
#ifdef RETRO_AND
   //you can change this after in core option if device support to setup a 832x576 res 
   retrow=640; 
   retroh=480;
   MOUSEMODE=1;
#endif

   update_variables();

   memset(Key_Sate,0,512);
   memset(Key_Sate2,0,512);

   if(!emuThread && !mainThread)
   {
      mainThread = co_active();
      emuThread = co_create(65536*sizeof(void*), retro_wrap_emulator);
   }

}

void Emu_uninit()
{
   texture_uninit();
}

void retro_shutdown_hatari(void)
{
   printf("SHUTDOWN\n");
   texture_uninit();
   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

void retro_reset(void){

}

//*****************************************************************************
//*****************************************************************************
// Disk control
extern bool Floppy_EjectDiskFromDrive(int Drive);
extern const char* Floppy_SetDiskFileName(int Drive, const char *pszFileName, const char *pszZipPath);
extern bool Floppy_InsertDiskIntoDrive(int Drive);

static bool disk_set_eject_state(bool ejected)
{
	if (dc)
	{
		dc->eject_state = ejected;
		
		if(dc->eject_state)
			return Floppy_EjectDiskFromDrive(0);
		else
			return Floppy_InsertDiskIntoDrive(0);			
	}
	
	return true;
}

static bool disk_get_eject_state(void)
{
	if (dc)
		return dc->eject_state;
	
	return true;
}

static unsigned disk_get_image_index(void)
{
	if (dc)
		return dc->index;
	
	return 0;
}

static bool disk_set_image_index(unsigned index)
{
	// Insert disk
	if (dc)
	{
		// Same disk...
		// This can mess things in the emu
		if(index == dc->index)
			return true;
		
		if ((index < dc->count) && (dc->files[index]))
		{
			dc->index = index;
			Floppy_SetDiskFileName(0, dc->files[index], NULL);
			log_cb(RETRO_LOG_INFO, "Disk (%d) inserted into drive A : %s\n", dc->index+1, dc->files[dc->index]);
			return true;
		}
	}
	
	return false;
}

static unsigned disk_get_num_images(void)
{
	if (dc)
		return dc->count;

	return 0;
}

static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
	// Not implemented
	// No many infos on this in the libretro doc...
	return false;
}

static bool disk_add_image_index(void)
{
	// Not implemented
	// No many infos on this in the libretro doc...
	return false;
}

static struct retro_disk_control_callback disk_interface = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

//*****************************************************************************
//*****************************************************************************
// Init
static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
}

void retro_init(void)
{    	
	struct retro_log_callback log;	
	const char *system_dir = NULL;
	dc = dc_create();

	// Init log
	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
		log_cb = log.log;
	else
		log_cb = fallback_log;

	if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
   {
      // if defined, use the system directory			
      retro_system_directory=system_dir;		
   }		   

   const char *content_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY, &content_dir) && content_dir)
   {
      // if defined, use the system directory			
      retro_content_directory=content_dir;		
   }			

   const char *save_dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) && save_dir)
   {
      // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *save_dir ? save_dir : retro_system_directory;      
   }
   else
   {
      // make retro_save_directory the same in case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY is not implemented by the frontend
      retro_save_directory=retro_system_directory;
   }

   if(retro_system_directory==NULL)sprintf(RETRO_DIR, "%s\0",".");
   else sprintf(RETRO_DIR, "%s\0", retro_system_directory);

   printf("Retro SYSTEM_DIRECTORY %s\n",retro_system_directory);
   printf("Retro SAVE_DIRECTORY %s\n",retro_save_directory);
   printf("Retro CONTENT_DIRECTORY %s\n",retro_content_directory);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "RGB565 is not supported.\n");
      exit(0);
   }

	struct retro_input_descriptor inputDescriptors[] = {
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3" },
		{ 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3" },
		// Terminate
		{ 0 }
	};
	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, &inputDescriptors);

   static struct retro_midi_interface midi_interface;

   if(environ_cb(RETRO_ENVIRONMENT_GET_MIDI_INTERFACE, &midi_interface))
      MidiRetroInterface = &midi_interface;
   else
      MidiRetroInterface = NULL;

 	// Disk control interface
	environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

	Emu_init();
	texture_init();
}

void retro_deinit(void)
{	 
   Emu_uninit(); 

   if(emuThread)
   {	 
      co_delete(emuThread);
      emuThread = 0;
   }

	// Clean the m3u storage
	if(dc)
		dc_free(dc);
   
   LOGI("Retro DeInit\n");
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Hatari";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "1.8" GIT_VERSION;
   info->valid_extensions = "ST|MSA|ZIP|STX|DIM|IPF|M3U";
   info->need_fullpath    = true;
   info->block_extract = false;

}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   struct retro_game_geometry geom = { retrow, retroh, 1024, 1024,4.0 / 3.0 };
   struct retro_system_timing timing = { 50.0, 44100.0 };

   info->geometry = geom;
   info->timing   = timing;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_run(void)
{
   int x;
   unsigned width = 640;
   unsigned height = 400;

   bool updated = false;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables();

   if(pauseg==0)
   {
      update_input();

      if(SND==1)
      {
         int16_t *p=(int16_t*)SNDBUF;

         for(x = 0; x < snd_sampler; x++)
            audio_cb(*p++,*p++);
      }
   }

   if(ConfigureParams.Screen.bAllowOverscan || SHOWKEY==1 || STATUTON==1 || pauseg==1 )
   {
      width  = retrow;
      height = retroh;
   }
   video_cb(bmp, width, height, retrow<< 1);

   co_switch(emuThread);

   if (MidiRetroInterface && MidiRetroInterface->output_enabled())
      MidiRetroInterface->flush();
}

#define M3U_FILE_EXT "m3u"

bool retro_load_game(const struct retro_game_info *info)
{
   // Init
   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, input_descriptors);   
   path_join(RETRO_TOS, RETRO_DIR, "tos.img");
   
   // Verify if tos.img is present
   if(!file_exists(RETRO_TOS))
   {
	   log_cb(RETRO_LOG_ERROR, "TOS image '%s' not found. Content cannot be loaded\n", RETRO_TOS);
	   return false;
   }

   const char *full_path;

   (void)info;

   full_path = info->path;

	// If it's a m3u file
	if(strendswith(full_path, M3U_FILE_EXT))
	{
		// Parse the m3u file
		dc_parse_m3u(dc, full_path);

		// Some debugging
		log_cb(RETRO_LOG_INFO, "m3u file parsed, %d file(s) found\n", dc->count);
		for(unsigned i = 0; i < dc->count; i++)
		{
			log_cb(RETRO_LOG_INFO, "file %d: %s\n", i+1, dc->files[i]);
		}	
	}
	else
	{
		// Add the file to disk control context
		// Maybe, in a later version of retroarch, we could add disk on the fly (didn't find how to do this)
		dc_add_file(dc, full_path);
	}

	// Init first disk
	dc->index = 0;
	dc->eject_state = false;
	log_cb(RETRO_LOG_INFO, "Disk (%d) inserted into drive A : %s\n", dc->index+1, dc->files[dc->index]);
	strcpy(RPATH,dc->files[0]);

	co_switch(emuThread);

   return true;
}

void retro_unload_game(void)
{
   pauseg=0;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data_, size_t size)
{
   return false;
}

bool retro_unserialize(const void *data_, size_t size)
{
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}

