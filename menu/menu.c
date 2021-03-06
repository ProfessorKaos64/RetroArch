/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "menu.h"
#include "menu_display.h"
#include "menu_entries.h"
#include "menu_shader.h"
#include "../dynamic.h"
#include "../frontend/frontend.h"
#include "../../retroarch.h"
#include "../../performance.h"
#include <file/file_path.h>

/**
 * menu_update_libretro_info:
 *
 * Update menu state which depends on config.
 **/
void menu_update_libretro_info(void)
{
   global_t *global               = global_get_ptr();
   struct retro_system_info *info = global ? &global->menu.info : NULL;

   if (!global || !info)
      return;

#ifndef HAVE_DYNAMIC
   retro_get_system_info(info);
#endif

   event_command(EVENT_CMD_CORE_INFO_INIT);
   menu_driver_context_reset();
   event_command(EVENT_CMD_LOAD_CORE_PERSIST);
}

static void menu_environment_get(int *argc, char *argv[],
      void *args, void *params_data)
{
   struct rarch_main_wrap *wrap_args = (struct rarch_main_wrap*)params_data;
   global_t *global     = global_get_ptr();
   settings_t *settings = config_get_ptr();
   menu_handle_t *menu  = menu_driver_get_ptr();
    
   if (!wrap_args)
      return;

   wrap_args->no_content       = menu->load_no_content;
   if (!global->has_set_verbosity)
      wrap_args->verbose       =  global->verbosity;

   wrap_args->config_path      = *global->config_path   ? global->config_path   : NULL;
   wrap_args->sram_path        = *global->savefile_dir  ? global->savefile_dir  : NULL;
   wrap_args->state_path       = *global->savestate_dir ? global->savestate_dir : NULL;
   wrap_args->content_path     = *global->fullpath      ? global->fullpath      : NULL;

   if (!global->has_set_libretro)
      wrap_args->libretro_path = *settings->libretro ? settings->libretro : NULL;
   wrap_args->touched       = true;
}

static void push_to_history_playlist(void)
{
   settings_t *settings = config_get_ptr();
   global_t *global     = global_get_ptr();

   if (!settings->history_list_enable)
      return;

   if (*global->fullpath)
   {
      char tmp[PATH_MAX_LENGTH];
      char str[PATH_MAX_LENGTH];

      fill_pathname_base(tmp, global->fullpath, sizeof(tmp));
      snprintf(str, sizeof(str), "INFO - Loading %s ...", tmp);
      rarch_main_msg_queue_push(str, 1, 1, false);
   }

   content_playlist_push(g_defaults.history,
         global->fullpath,
         settings->libretro,
         global->menu.info.library_name);
}

/**
 * menu_load_content:
 *
 * Loads content into currently selected core.
 * Will also optionally push the content entry to the history playlist.
 *
 * Returns: true (1) if successful, otherwise false (0).
 **/
bool menu_load_content(void)
{
   menu_handle_t *menu  = menu_driver_get_ptr();
   driver_t *driver     = driver_get_ptr();
   global_t *global     = global_get_ptr();

   /* redraw menu frame */
   if (menu)
      menu->msg_force = true;

   menu_driver_entry_iterate(MENU_ACTION_NOOP);

   menu_display_fb();

   if (!(main_load_content(0, NULL, NULL, menu_environment_get,
         driver->frontend_ctx->process_args)))
   {
      char name[PATH_MAX_LENGTH], msg[PATH_MAX_LENGTH];

      fill_pathname_base(name, global->fullpath, sizeof(name));
      snprintf(msg, sizeof(msg), "Failed to load %s.\n", name);
      rarch_main_msg_queue_push(msg, 1, 90, false);

      if (menu)
         menu->msg_force = true;

      return false;
   }

   menu_shader_manager_init(menu);

   event_command(EVENT_CMD_HISTORY_INIT);

   if (*global->fullpath || (menu && menu->load_no_content))
      push_to_history_playlist();

   event_command(EVENT_CMD_VIDEO_SET_ASPECT_RATIO);
   event_command(EVENT_CMD_RESUME);

   return true;
}

/**
 * menu_init:
 * @data                     : Menu context handle.
 *
 * Create and initialize menu handle.
 *
 * Returns: menu handle on success, otherwise NULL.
 **/
void *menu_init(const void *data)
{
   menu_handle_t *menu         = NULL;
   menu_ctx_driver_t *menu_ctx = (menu_ctx_driver_t*)data;
   runloop_t *runloop          = rarch_main_get_ptr();
   global_t  *global           = global_get_ptr();
   settings_t *settings        = config_get_ptr();

   if (!menu_ctx)
      return NULL;

   if (!(menu = (menu_handle_t*)menu_ctx->init()))
      return NULL;

   strlcpy(settings->menu.driver, menu_ctx->ident,
         sizeof(settings->menu.driver));

   if (!(menu->menu_list = (menu_list_t*)menu_list_new()))
      goto error;

   global->core_info_current = (core_info_t*)calloc(1, sizeof(core_info_t));
   if (!global->core_info_current)
      goto error;

#ifdef HAVE_SHADER_MANAGER
   menu->shader = (struct video_shader*)calloc(1, sizeof(struct video_shader));
   if (!menu->shader)
      goto error;
#endif
   menu->push_start_screen          = settings->menu_show_start_screen;
   settings->menu_show_start_screen = false;

   menu_shader_manager_init(menu);

   if (!menu_display_init(menu))
      goto error;

   rarch_assert(menu->msg_queue = msg_queue_new(8));

   runloop->frames.video.current.menu.framebuf.dirty = true;

   return menu;
error:
   if (menu->menu_list)
      menu_list_free(menu->menu_list);
   menu->menu_list = NULL;
   if (global->core_info_current)
      free(global->core_info_current);
   global->core_info_current = NULL;
   if (menu->shader)
      free(menu->shader);
   menu->shader = NULL;
   if (menu)
      free(menu);
   return NULL;
}


/**
 * menu_free_list:
 * @menu                     : Menu handle.
 *
 * Frees menu lists.
 **/
void menu_free_list(menu_handle_t *menu)
{
   if (!menu)
      return;

   settings_list_free(menu->list_settings);
   menu->list_settings = NULL;
}

/**
 * menu_free:
 * @menu                     : Menu handle.
 *
 * Frees a menu handle
 **/
void menu_free(menu_handle_t *menu)
{
   global_t *global    = global_get_ptr();

   if (!menu)
      return;
  
#ifdef HAVE_SHADER_MANAGER
   if (menu->shader)
      free(menu->shader);
   menu->shader = NULL;
#endif

   menu_driver_free(menu);

#ifdef HAVE_LIBRETRODB
   menu_database_free(menu);
#endif

#ifdef HAVE_DYNAMIC
   libretro_free_system_info(&global->menu.info);
#endif

   if (menu->msg_queue)
      msg_queue_free(menu->msg_queue);
   menu->msg_queue = NULL;

   menu_display_free(menu);

   if (menu->frame_buf.data)
      free(menu->frame_buf.data);
   menu->frame_buf.data = NULL;

   menu_list_free(menu->menu_list);
   menu->menu_list = NULL;

   event_command(EVENT_CMD_HISTORY_DEINIT);

   if (global->core_info)
      core_info_list_free(global->core_info);

   if (global->core_info_current)
      free(global->core_info_current);
}

void menu_apply_deferred_settings(void)
{
   menu_handle_t   *menu    = menu_driver_get_ptr();
   rarch_setting_t *setting = menu ? menu->list_settings : NULL;
    
   if (!menu || !setting)
      return;
    
   for (; setting->type != ST_NONE; setting++)
   {
      if (setting->type >= ST_GROUP)
         continue;

      if (!(setting->flags & SD_FLAG_IS_DEFERRED))
         continue;

      switch (setting->type)
      {
         case ST_BOOL:
            if (*setting->value.boolean != setting->original_value.boolean)
            {
               setting->original_value.boolean = *setting->value.boolean;
               setting->deferred_handler(setting);
            }
            break;
         case ST_INT:
            if (*setting->value.integer != setting->original_value.integer)
            {
               setting->original_value.integer = *setting->value.integer;
               setting->deferred_handler(setting);
            }
            break;
         case ST_UINT:
            if (*setting->value.unsigned_integer != setting->original_value.unsigned_integer)
            {
               setting->original_value.unsigned_integer = *setting->value.unsigned_integer;
               setting->deferred_handler(setting);
            }
            break;
         case ST_FLOAT:
            if (*setting->value.fraction != setting->original_value.fraction)
            {
               setting->original_value.fraction = *setting->value.fraction;
               setting->deferred_handler(setting);
            }
            break;
         case ST_PATH:
         case ST_DIR:
         case ST_STRING:
         case ST_BIND:
            /* Always run the deferred write handler */
            setting->deferred_handler(setting);
            break;
         default:
            break;
      }
   }
}

/**
 * menu_iterate:
 * @input                    : input sample for this frame
 * @old_input                : input sample of the previous frame
 * @trigger_input            : difference' input sample - difference
 *                             between 'input' and 'old_input'
 *
 * Runs RetroArch menu for one frame.
 *
 * Returns: 0 on success, -1 if we need to quit out of the loop. 
 **/
int menu_iterate(retro_input_t input,
      retro_input_t old_input, retro_input_t trigger_input)
{
   static retro_time_t last_clock_update = 0;
   int32_t ret          = 0;
   unsigned action      = menu_input_frame(input, trigger_input);
   runloop_t *runloop   = rarch_main_get_ptr();
   menu_handle_t *menu  = menu_driver_get_ptr();
   settings_t *settings = config_get_ptr();

   menu->cur_time       = rarch_get_time_usec();
   menu->dt             = menu->cur_time - menu->old_time;

   if (menu->dt >= IDEAL_DT * 4)
      menu->dt = IDEAL_DT * 4;
   if (menu->dt <= IDEAL_DT / 4)
      menu->dt = IDEAL_DT / 4;
   menu->old_time = menu->cur_time;

   if (menu->cur_time - last_clock_update > 1000000 && settings->menu.timedate_enable)
   {
      runloop->frames.video.current.menu.label.is_updated = true;
      last_clock_update = menu->cur_time;
   }

   menu_driver_entry_iterate(action);

   if (runloop->is_menu && !runloop->is_idle)
      menu_display_fb();

   menu_driver_set_texture();

   if (ret)
      return -1;

   return 0;
}
