/* Nessus Attack Scripting Language
 *
 * Copyright (C) 2002 - 2005 Tenable Network Security
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

 /**
  * @file
  * Source of the standalone NASL interpreter of OpenVAS.
  */

#include <signal.h>             /* for SIGINT */
#include <string.h>             /* for strlen */
#include <stdlib.h>             /* for exit */
#include <unistd.h>             /* for geteuid */
#include <libssh/libssh.h>      /* for ssh_version */

#include "kb.h"                 /* for kb_new */
#include "network.h"

#include "nasl.h"
#include "nasl_lex_ctxt.h"
#include "exec.h"
#include "../base/gpgme_util.h" /* for gpgme_check_version */
#include <../base/openvas_hosts.h> /* for openvas_hosts_* and openvas_host_* */
#include <../base/nvti.h>
#include <../misc/prefs.h> /* for prefs_get */
#include <glib.h>
#ifndef MAP_FAILED
#define MAP_FAILED ((void*)-1)
#endif


extern char *nasl_version (void);
extern int execute_instruction (struct arglist *, char *);
void exit_nasl (struct arglist *, int);


int safe_checks_only = 0;

static struct arglist *
init_hostinfos (char *hostname, struct in6_addr *ip)
{
  struct arglist *hostinfos;

  hostinfos = g_malloc0 (sizeof (struct arglist));
  arg_add_value (hostinfos, "FQDN", ARG_STRING, hostname);
  arg_add_value (hostinfos, "NAME", ARG_STRING, hostname);
  arg_add_value (hostinfos, "IP", ARG_PTR, ip);
  return (hostinfos);
}

void
sighandler (int s)
{
  exit (0);
}

static void
my_gnutls_log_func (int level, const char *text)
{
  fprintf (stderr, "[%d] (%d) %s", getpid (), level, text);
  if (*text && text[strlen (text) -1] != '\n')
    putc ('\n', stderr);
}

struct arglist *
init (char *hostname, struct in6_addr ip, kb_t kb)
{
  struct arglist *script_infos = g_malloc0 (sizeof (struct arglist));
  struct in6_addr *pip = g_malloc0 (sizeof (*pip));

  memcpy (pip, &ip, sizeof (struct in6_addr));

  arg_add_value (script_infos, "standalone", ARG_INT, (void *) 1);
  prefs_set ("checks_read_timeout", "5");
  arg_add_value (script_infos, "key", ARG_PTR, kb);

  if (safe_checks_only != 0)
    prefs_set ("safe_checks", "yes");

  arg_add_value (script_infos, "HOSTNAME", ARG_ARGLIST,
                 init_hostinfos (hostname, pip));

  return script_infos;
}

extern FILE *nasl_trace_fp;

static int
parse_script_infos (const char *file, struct arglist *script_infos)
{
  nvti_t *nvti;
  char *oid;
  int mode = NASL_EXEC_DESCR | NASL_ALWAYS_SIGNED;

  nvti = nvti_new ();
  arg_add_value (script_infos, "NVTI", ARG_PTR, nvti);

 if (exec_nasl_script (script_infos, file, NULL, mode) < 0)
    {
      printf ("%s could not be loaded\n", file);
      return 1;
    }
  arg_del_value (script_infos, "NVTI");
  arg_del_value (script_infos, "OID");
  oid = g_strdup (nvti_oid (nvti));
  nvti_free (nvti);
  if (oid)
    arg_add_value (script_infos, "OID", ARG_STRING, oid);




  return 0;
}

/**
 * @brief Main of the standalone nasl interpretor.
 * @return The number of times a NVT was launched
 *         (should be (number of targets) * (number of NVTS provided)).
 */

extern int executeNasl(struct ExternalData* definition,DaltonScriptInfo *daltonScriptInfo)
{
  //do initialize the dalton info object
  initializeDaltonInfo();
  struct arglist *script_infos;
  openvas_hosts_t *hosts;
  openvas_host_t *host;
  static gchar *target = NULL;
  gchar *default_target = "127.0.0.1";
  int start, n;
  int mode = 0;
  int err = 0;
  static gboolean both_modes = FALSE;
  static gchar *config_file = NULL;
  static gboolean authenticated_mode = FALSE;
  static gchar **nasl_filenames = NULL;
  nasl_filenames = &definition->file;
  authenticated_mode = definition->authenticated;
  target = definition->target;

  if (authenticated_mode)
  {
    mode |= NASL_ALWAYS_SIGNED;
  }

  openvas_SSL_init ();
  if (!target)
    target = g_strdup (default_target);
  start = 0;
  hosts = openvas_hosts_new (target);
  //g_free (target);

  // for absolute and relative paths
  add_nasl_inc_dir ("");
  prefs_config (config_file ?: OPENVASSD_CONF);
  while ((host = openvas_hosts_next (hosts)))
  {
    struct in6_addr ip6;
    char *hostname;
    kb_t kb;
    //int rc;

    hostname = openvas_host_value_str (host);
    if (openvas_host_get_addr6 (host, &ip6) == -1)
    {
      fprintf (stderr, "Couldn't resolve %s\n", hostname);
      err++;
      g_free (hostname);
      continue;
    }

    kb_new (&kb, prefs_get ("kb_location") ?: KB_PATH_DEFAULT);
   /*if (rc)
      exit (1);*/

    script_infos = init (hostname, ip6, kb);

    n = start;
    while (n < 1)
    {
      if(nasl_filenames[n] == NULL || nasl_filenames[n] =='\0') continue;
      if (both_modes && parse_script_infos (nasl_filenames[n],
                                            script_infos))
        err++;
      else if (exec_nasl_script (script_infos, nasl_filenames[n],
                                 arg_get_value (script_infos, "OID"), mode) < 0)
        err++;
      n++;
    }
    kb_delete (kb);
    g_free (hostname);
  }
  openvas_hosts_free (hosts);
  memcpy(daltonScriptInfo,daltonInfo,sizeof(DaltonScriptInfo));
  //memcpy(daltonScriptInfo->ScriptCveIds,daltonInfo->ScriptCveIds,sizeof(daltonInfo->ScriptCveIds));

  clearDaltonInfo();



  return 0;
}
int clearDaltonContainer(DaltonScriptInfo *container)
{
  if(container)
  {
    free(container);
    clearDaltonInfo();
  }

  return 0;
}

int clearDaltonInfo()
{
  if(daltonInfo) //If dalton info is already initialized so delete it
  {
    //Freeing it from memory
    free(daltonInfo);
     xrefCount = 0;
     tagsCount = 0;
     addPreferencesCount = 0;
     SecurityMessagesCount = 0;
  }


  return 0;

}
int
main (int argc, char **argv)
{

  struct ExternalData *definition = malloc(sizeof(struct ExternalData));
  definition->target = "192.168.1.8";
  definition->file = "/media/snouto/rest/projects/openvas/nvts/gb_default_smb_credentials.nasl";
  definition->authenticated = 1;

  DaltonScriptInfo *testInfo = (DaltonScriptInfo *)malloc(sizeof(DaltonScriptInfo));
  int result =  executeNasl(definition,testInfo);
  fprintf(stdout,"Script version is : %s\n",testInfo->ScriptVersion);

  //test printing the version of the script
  //finally return the results
  return result;
}

int
mainc (int argc, char **argv)
{
  struct arglist *script_infos;
  openvas_hosts_t *hosts;
  openvas_host_t *host;
  static gchar *target = NULL;
  gchar *default_target = "127.0.0.1";
  int start, n;
  int mode = 0;
  int err = 0;
  extern int global_nasl_debug;

  static gboolean display_version = FALSE;
  static gboolean nasl_debug = FALSE;
  static gboolean description_only = FALSE;
  static gboolean both_modes = FALSE;
  static gboolean parse_only = FALSE;
  static gboolean do_lint = FALSE;
  static gchar *trace_file = NULL;
  static gchar *config_file = NULL;
  static gchar *source_iface = NULL;
  static gboolean with_safe_checks = FALSE;
  static gboolean authenticated_mode = FALSE;
  static gchar *include_dir = NULL;
  static gchar **nasl_filenames = NULL;
  static int debug_tls = 0;
  GError *error = NULL;
  GOptionContext *option_context;
  static GOptionEntry entries[] = {
    {"version", 'V', 0, G_OPTION_ARG_NONE, &display_version,
     "Display version information", NULL},
    {"debug", 'd', 0, G_OPTION_ARG_NONE, &nasl_debug,
     "Output debug information to stderr.", NULL},
    {"description", 'D', 0, G_OPTION_ARG_NONE, &description_only,
     "Only run the 'description' part of the script", NULL},
    {"both", 'B', 0, G_OPTION_ARG_NONE, &both_modes,
     "Run in description mode before running the script.", NULL},
    {"parse", 'p', 0, G_OPTION_ARG_NONE, &parse_only,
     "Only parse the script, don't execute it", NULL},
    {"lint", 'L', 0, G_OPTION_ARG_NONE, &do_lint,
     "'lint' the script (extended checks)", NULL},
    {"target", 't', 0, G_OPTION_ARG_STRING, &target,
     "Execute the scripts against <target>", "<target>"},
    {"trace", 'T', 0, G_OPTION_ARG_FILENAME, &trace_file,
     "Log actions to <file> (or '-' for stderr)", "<file>"},
    {"config-file", 'c', 0, G_OPTION_ARG_FILENAME, &config_file,
     "Configuration file", "<filenmae>"},
    {"source-iface", 'e', 0, G_OPTION_ARG_STRING, &source_iface,
     "Source network interface for established connections.",
     "<iface_name>"},
    {"safe", 's', 0, G_OPTION_ARG_NONE, &with_safe_checks,
     "Specifies that the script should be run with 'safe checks' enabled",
     NULL},
    {"authenticated", 'X', 0, G_OPTION_ARG_NONE, &authenticated_mode,
     "Run the script in 'authenticated' mode", NULL},
    {"include-dir", 'i', 0, G_OPTION_ARG_STRING, &include_dir,
     "Search for includes in <dir>", "<dir>"},
    {"debug-tls", 0, 0, G_OPTION_ARG_INT, &debug_tls,
     "Enable TLS debugging at <level>", "<level>"},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &nasl_filenames,
     "Absolute path to one or more nasl scripts", "NASL_FILE..."},
    {NULL}
  };

  option_context =
    g_option_context_new ("- standalone NASL interpreter for OpenVAS");
  g_option_context_add_main_entries (option_context, entries, NULL);
  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      g_print ("%s\n\n", error->message);
      exit (0);
    }
  g_option_context_free (option_context);
 /*--------------------------------------------
 	Command-line options
  ---------------------------------------------*/

  if (display_version)
    {
      printf ("openvas-nasl %s\n", nasl_version ());
      if (debug_tls)
        {
          printf ("gnutls %s\n", gnutls_check_version (NULL));
          printf ("libssh %s\n", ssh_version (0));
          printf ("gpgme %s\n", gpgme_check_version (NULL));
        }
      else
        putchar ('\n');
      printf ("Copyright (C) 2002 - 2004 Tenable Network Security\n");
      printf ("Copyright (C) 2013 Greenbone Networks GmbH\n\n");
      exit (0);
    }
  if (nasl_debug)
    global_nasl_debug = 1;
  mode |= NASL_COMMAND_LINE;
  if (authenticated_mode)
    mode |= NASL_ALWAYS_SIGNED;
  if (description_only)
    mode |= NASL_EXEC_DESCR;
  if (do_lint)
    mode |= NASL_LINT;
  if (parse_only)
    mode |= NASL_EXEC_PARSE_ONLY;
  if (trace_file)
    {
      if (!strcmp (trace_file, "-"))
        nasl_trace_fp = stderr;
      else
        {
          FILE *fp = fopen (trace_file, "w");
          if (fp == NULL)
            {
              perror (optarg);
              exit (2);
            }
          setvbuf (fp, NULL, _IOLBF, BUFSIZ);
          nasl_trace_fp = fp;
        }
    }
  if (with_safe_checks)
    safe_checks_only++;

  openvas_SSL_init ();
  if (!nasl_filenames)
    {
      fprintf (stderr, "Error. No input file(s) specified !\n");
      exit (1);
    }

#ifndef _CYGWIN_
  if (!(mode & (NASL_EXEC_PARSE_ONLY | NASL_LINT)) && geteuid ())
    {
      fprintf (stderr, "** WARNING : packet forgery will not work\n");
      fprintf (stderr, "** as NASL is not running as root\n");
    }
  signal (SIGINT, sighandler);
  signal (SIGTERM, sighandler);
  signal (SIGPIPE, SIG_IGN);
#endif

  if (source_iface && openvas_source_iface_init (source_iface))
    {
      fprintf (stderr, "Erroneous network source interface: %s\n",
               source_iface);
      exit (1);
    }
  if (debug_tls)
    {
      gnutls_global_set_log_function (my_gnutls_log_func);
      gnutls_global_set_log_level (debug_tls);
    }

  if (!target)
    target = g_strdup (default_target);

  start = 0;

  hosts = openvas_hosts_new (target);
  g_free (target);

  // for absolute and relative paths
  add_nasl_inc_dir ("");
  if (include_dir != NULL)
    {
      add_nasl_inc_dir (include_dir);
    }

  prefs_config (config_file ?: OPENVASSD_CONF);
  while ((host = openvas_hosts_next (hosts)))
    {
      struct in6_addr ip6;
      char *hostname;
      kb_t kb;
      int rc;

      hostname = openvas_host_value_str (host);
      if (openvas_host_get_addr6 (host, &ip6) == -1)
        {
          fprintf (stderr, "Couldn't resolve %s\n", hostname);
          err++;
          g_free (hostname);
          continue;
        }

      rc = kb_new (&kb, prefs_get ("kb_location") ?: KB_PATH_DEFAULT);
      if (rc)
        exit (1);

      script_infos = init (hostname, ip6, kb);
      n = start;
      while (nasl_filenames[n])
        {
          if (both_modes && parse_script_infos (nasl_filenames[n],
                                                script_infos))
            err++;
          else if (exec_nasl_script (script_infos, nasl_filenames[n],
                                     arg_get_value (script_infos, "OID"), mode) < 0)
            err++;
          n++;
        }
      kb_delete (kb);
      g_free (hostname);
    }

  if (nasl_trace_fp != NULL)
    fflush (nasl_trace_fp);

  openvas_hosts_free (hosts);
  return err;
}
