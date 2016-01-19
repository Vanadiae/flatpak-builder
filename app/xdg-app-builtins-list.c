/*
 * Copyright © 2014 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "libgsystem.h"
#include "libglnx/libglnx.h"

#include "xdg-app-builtins.h"
#include "xdg-app-utils.h"

static gboolean opt_show_details;
static gboolean opt_user;
static gboolean opt_system;
static gboolean opt_runtime;
static gboolean opt_app;

static GOptionEntry options[] = {
  { "user", 0, 0, G_OPTION_ARG_NONE, &opt_user, "Show user installations", NULL },
  { "system", 0, 0, G_OPTION_ARG_NONE, &opt_system, "Show system-wide installations", NULL },
  { "show-details", 'd', 0, G_OPTION_ARG_NONE, &opt_show_details, "Show arches and branches", NULL },
  { "runtime", 0, 0, G_OPTION_ARG_NONE, &opt_runtime, "List installed runtimes", },
  { "app", 0, 0, G_OPTION_ARG_NONE, &opt_app, "List installed applications", },
  { NULL }
};

static char **
join_strv (char **a, char **b)
{
  gsize len = 1, i, j;
  char **res;

  if (a)
    len += g_strv_length (a);
  if (b)
    len += g_strv_length (b);

  res = g_new (char *, len);

  i = 0;

  for (j = 0; a != NULL && a[j] != NULL; j++)
    res[i++] = g_strdup (a[j]);

  for (j = 0; b != NULL && b[j] != NULL; j++)
    res[i++] = g_strdup (b[j]);

  res[i++] = NULL;
  return res;
}

static gboolean
print_installed_refs (gboolean app, gboolean runtime, gboolean print_system, gboolean print_user, GCancellable *cancellable, GError **error)
{
  g_autofree char *last = NULL;
  g_auto(GStrv) system = NULL;
  g_auto(GStrv) system_app = NULL;
  g_auto(GStrv) system_runtime = NULL;
  g_auto(GStrv) user = NULL;
  g_auto(GStrv) user_app = NULL;
  g_auto(GStrv) user_runtime = NULL;
  int s, u;

  if (print_user)
    {
      g_autoptr(XdgAppDir) dir = NULL;

      dir = xdg_app_dir_get (TRUE);

      if (xdg_app_dir_ensure_repo (dir, cancellable, NULL))
        {
          if (app && !xdg_app_dir_list_refs (dir, "app", &user_app, cancellable, error))
            return FALSE;
          if (runtime && !xdg_app_dir_list_refs (dir, "runtime", &user_runtime, cancellable, error))
            return FALSE;
        }
    }

  if (print_system)
    {
      g_autoptr(XdgAppDir) dir = NULL;

      dir = xdg_app_dir_get (FALSE);
      if (xdg_app_dir_ensure_repo (dir, cancellable, NULL))
        {
          if (app && !xdg_app_dir_list_refs (dir, "app", &system_app, cancellable, error))
            return FALSE;
          if (runtime && !xdg_app_dir_list_refs (dir, "runtime", &system_runtime, cancellable, error))
            return FALSE;
        }
    }

  XdgAppTablePrinter *printer = xdg_app_table_printer_new ();

  user = join_strv (user_app, user_runtime);
  system = join_strv (system_app, system_runtime);

  for (s = 0, u = 0; system[s] != NULL || user[u] != NULL; )
    {
      char *ref, *partial_ref;
      g_auto(GStrv) parts = NULL;
      g_autofree char *repo = NULL;
      gboolean is_user;
      g_autoptr(XdgAppDir) dir = NULL;

      if (system[s] == NULL)
        is_user = TRUE;
      else if (user[u] == NULL)
        is_user = FALSE;
      else if (strcmp (system[s], user[u]) <= 0)
        is_user = FALSE;
      else
        is_user = TRUE;

      if (is_user)
        ref = user[u++];
      else
        ref = system[s++];

      parts = g_strsplit (ref, "/", -1);
      partial_ref = strchr(ref, '/') + 1;

      dir = xdg_app_dir_get (is_user);
      repo = xdg_app_dir_get_origin (dir, ref, NULL, NULL);

      if (opt_show_details)
        {
          g_autofree char *active = xdg_app_dir_read_active (dir, ref, NULL);
          g_autofree char *latest = NULL;

          latest = xdg_app_dir_read_latest (dir, repo, ref, NULL, NULL);
          if (latest)
            {
              if (strcmp (active, latest) == 0)
                {
                  g_free (latest);
                  latest = g_strdup ("-");
                }
              else
                latest[MIN(strlen(latest), 12)] = 0;
            }
          else
            latest = g_strdup ("?");

          xdg_app_table_printer_add_column (printer, partial_ref);
          xdg_app_table_printer_add_column (printer, repo);

          active[MIN(strlen(active), 12)] = 0;
          xdg_app_table_printer_add_column (printer, active);
          xdg_app_table_printer_add_column (printer, latest);

          xdg_app_table_printer_add_column (printer, ""); /* Options */

          if (print_user && print_system)
            xdg_app_table_printer_append_with_comma (printer, is_user ? "user" : "system");

          if (strcmp (parts[0], "app") == 0)
            {
              g_autofree char *current;

              current = xdg_app_dir_current_ref (dir, parts[1], cancellable);
              if (current && strcmp (ref, current) == 0)
                xdg_app_table_printer_append_with_comma (printer, "current");
            }
          else
            {
              if (app)
                xdg_app_table_printer_append_with_comma (printer, "runtime");
            }
        }
      else
        {
          if (last == NULL || strcmp (last, parts[1]) != 0)
            {
              xdg_app_table_printer_add_column (printer, parts[1]);
              g_clear_pointer (&last, g_free);
              last = g_strdup (parts[1]);
            }
        }
      xdg_app_table_printer_finish_row (printer);
    }

  xdg_app_table_printer_print (printer);
  xdg_app_table_printer_free (printer);

  return TRUE;
}

gboolean
xdg_app_builtin_list (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  g_autoptr(GOptionContext) context = NULL;

  context = g_option_context_new (" - List installed apps and/or runtimes");

  if (!xdg_app_option_context_parse (context, options, &argc, &argv, XDG_APP_BUILTIN_FLAG_NO_DIR, NULL, cancellable, error))
    return FALSE;

  if (!opt_app && !opt_runtime)
    opt_app = TRUE;

  if (!print_installed_refs (opt_app, opt_runtime,
                             opt_system || (!opt_user && !opt_system),
                             opt_user || (!opt_user && !opt_system),
                             cancellable, error))
    return FALSE;

  return TRUE;
}

gboolean
xdg_app_builtin_list_runtimes (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  opt_runtime = TRUE;
  return xdg_app_builtin_list (argc, argv, cancellable, error);
}

gboolean
xdg_app_builtin_list_apps (int argc, char **argv, GCancellable *cancellable, GError **error)
{
  opt_app = TRUE;
  return xdg_app_builtin_list (argc, argv, cancellable, error);
}
