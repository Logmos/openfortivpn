/*
 *  Copyright (C) 2015 Adrien Vergé
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>


const struct vpn_config invalid_cfg = {
	.gateway_host = {'\0'},
	.gateway_port = 0,
	.username = {'\0'},
	.password = {'\0'},
	.otp = {'\0'},
	.realm = {'\0'},
	.set_routes = -1,
	.set_dns = -1,
	.pppd_use_peerdns = -1,
	.use_syslog = -1,
	.half_internet_routes = -1,
	.persistent = -1,
	.pppd_log = NULL,
	.pppd_plugin = NULL,
	.pppd_ipparam = NULL,
	.pppd_ifname = NULL,
	.pppd_call = NULL,
	.ca_file = NULL,
	.user_cert = NULL,
	.user_key = NULL,
	.insecure_ssl = -1,
	.cipher_list = NULL,
	.cert_whitelist = NULL
};

/*
 * Adds a sha256 digest to the list of trusted certificates.
 */
int add_trusted_cert(struct vpn_config *cfg, const char *digest)
{
	struct x509_digest *last, *new;

	new = malloc(sizeof(struct x509_digest));
	if (new == NULL)
		return ERR_CFG_NO_MEM;

	new->next = NULL;
	strncpy(new->data, digest, SHA256STRLEN - 1);
	new->data[SHA256STRLEN - 1] = '\0';

	if (cfg->cert_whitelist == NULL) {
		cfg->cert_whitelist = new;
	} else {
		for (last = cfg->cert_whitelist; last->next != NULL;
		     last = last->next)
			;
		last->next = new;
	}

	return 0;
}

/*
 * Converts string to bool int
 *
 * @params[in] str  the string to read from
 * @return          0 or 1 if successful, < 0 if unrecognized value
 */
int strtob(const char *str)
{
	if (str[0] == '\0')
		return 0;
	else if (strcasecmp(str, "true") == 0)
		return 1;
	else if (strcasecmp(str, "false") == 0)
		return 0;
	else if (isdigit(str[0]) == 0)
		return -1;

	long int i = strtol(str, NULL, 0);
	if (i < 0 || i > 1)
		return -1;
	return i;
}

/*
 * Reads filename contents and fill cfg with its values.
 *
 * @param[out] cfg       the struct vpn_config to store configuration values
 * @param[in]  filename  the file to read values from
 * @return               0 if successful, or < 0 in case of error
 */
int load_config(struct vpn_config *cfg, const char *filename)
{
	int ret = ERR_CFG_UNKNOWN;
	FILE *file;
	struct stat stat;
	char *buffer, *line;

	file = fopen(filename, "r");
	if (file == NULL)
		return ERR_CFG_SEE_ERRNO;

	if (fstat(fileno(file), &stat) == -1) {
		ret = ERR_CFG_SEE_ERRNO;
		goto err_close;
	}
	if (stat.st_size == 0) {
		ret = ERR_CFG_EMPTY_FILE;
		goto err_close;
	}

	buffer = malloc(stat.st_size + 1);
	if (buffer == NULL) {
		ret = ERR_CFG_NO_MEM;
		goto err_close;
	}

	// Copy all file contents at once
	if (fread(buffer, stat.st_size, 1, file) != 1) {
		ret = ERR_CFG_CANNOT_READ;
		goto err_free;
	}

	buffer[stat.st_size] = '\0';

	// Read line by line
	for (line = strtok(buffer, "\n"); line != NULL;
	     line = strtok(NULL, "\n")) {
		char *key, *equals, *val;
		int i;

		if (line[0] == '#')
			continue;

		// Expect something like: "key = value"
		equals = strchr(line, '=');
		if (equals == NULL) {
			log_warn("Bad line in config file: \"%s\".\n", line);
			continue;
		}
		equals[0] = '\0';
		key = line;
		val = equals + 1;

		// Remove heading spaces
		while (isspace(key[0]))
			key++;
		while (isspace(val[0]))
			val++;
		// Remove trailing spaces
		for (i = strlen(key) - 1; i > 0; i--) {
			if (isspace(key[i]))
				key[i] = '\0';
			else
				break;
		}
		for (i = strlen(val) - 1; i > 0; i--) {
			if (isspace(val[i]))
				val[i] = '\0';
			else
				break;
		}

		if (strcmp(key, "host") == 0) {
			strncpy(cfg->gateway_host, val, FIELD_SIZE);
			cfg->gateway_host[FIELD_SIZE] = '\0';
		} else if (strcmp(key, "port") == 0) {
			long int port = strtol(val, NULL, 0);
			if (port <= 0 || port > 65535) {
				log_warn("Bad port in config file: \"%d\".\n",
				         port);
				continue;
			}
			cfg->gateway_port = port;
		} else if (strcmp(key, "username") == 0) {
			strncpy(cfg->username, val, FIELD_SIZE - 1);
			cfg->username[FIELD_SIZE] = '\0';
		} else if (strcmp(key, "password") == 0) {
			strncpy(cfg->password, val, FIELD_SIZE - 1);
			cfg->password[FIELD_SIZE] = '\0';
		} else if (strcmp(key, "otp") == 0) {
			strncpy(cfg->otp, val, FIELD_SIZE - 1);
			cfg->otp[FIELD_SIZE] = '\0';
		} else if (strcmp(key, "realm") == 0) {
			strncpy(cfg->realm, val, FIELD_SIZE - 1);
			cfg->realm[FIELD_SIZE] = '\0';
		} else if (strcmp(key, "set-dns") == 0) {
			int set_dns = strtob(val);
			if (set_dns < 0) {
				log_warn("Bad set-dns in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->set_dns = set_dns;
		} else if (strcmp(key, "set-routes") == 0) {
			int set_routes = strtob(val);
			if (set_routes < 0) {
				log_warn("Bad set-routes in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->set_routes = set_routes;
		} else if (strcmp(key, "half-internet-routes") == 0) {
			int half_internet_routes = strtob(val);
			if (half_internet_routes < 0) {
				log_warn("Bad half-internet-routes in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->half_internet_routes = half_internet_routes;
		} else if (strcmp(key, "persistent") == 0) {
			long int persistent = strtol(val, NULL, 0);
			if (persistent < 0 || persistent > UINT_MAX) {
				log_warn("Bad value for persistent in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->persistent = persistent;
		} else if (strcmp(key, "pppd-use-peerdns") == 0) {
			int pppd_use_peerdns = strtob(val);
			if (pppd_use_peerdns < 0) {
				log_warn("Bad pppd-use-peerdns in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->pppd_use_peerdns = pppd_use_peerdns;
		} else if (strcmp(key, "pppd-log") == 0) {
			free(cfg->pppd_log);
			cfg->pppd_log = strdup(val);
		} else if (strcmp(key, "pppd-plugin") == 0) {
			free(cfg->pppd_plugin);
			cfg->pppd_plugin = strdup(val);
		} else if (strcmp(key, "pppd-ipparam") == 0) {
			free(cfg->pppd_ipparam);
			cfg->pppd_ipparam = strdup(val);
		} else if (strcmp(key, "pppd-ifname") == 0) {
			free(cfg->pppd_ifname);
			cfg->pppd_ifname = strdup(val);
		} else if (strcmp(key, "pppd-call") == 0) {
			free(cfg->pppd_call);
			cfg->pppd_call = strdup(val);
		} else if (strcmp(key, "use-syslog") == 0) {
			int use_syslog = strtob(val);
			if (use_syslog < 0) {
				log_warn("Bad use-syslog in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->use_syslog = use_syslog;
		} else if (strcmp(key, "trusted-cert") == 0) {
			if (strlen(val) != SHA256STRLEN - 1) {
				log_warn("Bad certificate sha256 digest in config file: \"%s\".\n",
				         val);
				continue;
			}
			if (add_trusted_cert(cfg, val))
				log_warn("Could not add certificate digest to whitelist.\n");

		} else if (strcmp(key, "ca-file") == 0) {
			free(cfg->ca_file);
			cfg->ca_file = strdup(val);
		} else if (strcmp(key, "user-cert") == 0) {
			free(cfg->user_cert);
			cfg->user_cert = strdup(val);
		} else if (strcmp(key, "user-key") == 0) {
			free(cfg->user_key);
			cfg->user_key = strdup(val);
		} else if (strcmp(key, "insecure-ssl") == 0) {
			int insecure_ssl = strtob(val);
			if (insecure_ssl < 0) {
				log_warn("Bad insecure-ssl in config file: \"%s\".\n",
				         val);
				continue;
			}
			cfg->insecure_ssl = insecure_ssl;
		} else if (strcmp(key, "cipher-list") == 0) {
			free(cfg->cipher_list);
			cfg->cipher_list = strdup(val);
		} else {
			log_warn("Bad key in config file: \"%s\".\n", key);
			goto err_free;
		}
	}

	ret = 0;

err_free:
	free(buffer);
err_close:
	fclose(file);

	return ret;
}

void destroy_vpn_config(struct vpn_config *cfg)
{
	free(cfg->pppd_log);
	free(cfg->pppd_plugin);
	free(cfg->pppd_ipparam);
	free(cfg->pppd_ifname);
	free(cfg->pppd_call);
	free(cfg->ca_file);
	free(cfg->user_cert);
	free(cfg->user_key);
	free(cfg->cipher_list);
	while (cfg->cert_whitelist != NULL) {
		struct x509_digest *tmp = cfg->cert_whitelist->next;
		free(cfg->cert_whitelist);
		cfg->cert_whitelist = tmp;
	}
}

void merge_config(struct vpn_config *dst, struct vpn_config *src)
{
	if (src->gateway_host[0])
		strcpy(dst->gateway_host, src->gateway_host);
	if (src->gateway_port != invalid_cfg.gateway_port)
		dst->gateway_port = src->gateway_port;
	if (src->username[0])
		strcpy(dst->username, src->username);
	if (src->password[0])
		strcpy(dst->password, src->password);
	if (src->otp[0])
		strcpy(dst->otp, src->otp);
	if (src->realm[0])
		strcpy(dst->realm, src->realm);
	if (src->set_routes != invalid_cfg.set_routes)
		dst->set_routes = src->set_routes;
	if (src->set_dns != invalid_cfg.set_dns)
		dst->set_dns = src->set_dns;
	if (src->pppd_use_peerdns != invalid_cfg.pppd_use_peerdns)
		dst->pppd_use_peerdns = src->pppd_use_peerdns;
	if (src->use_syslog != invalid_cfg.use_syslog)
		dst->use_syslog = src->use_syslog;
	if (src->half_internet_routes != invalid_cfg.half_internet_routes)
		dst->half_internet_routes = src->half_internet_routes;
	if (src->persistent != invalid_cfg.persistent)
		dst->persistent = src->persistent;
	if (src->pppd_log) {
		free(dst->pppd_log);
		dst->pppd_log = src->pppd_log;
	}
	if (src->pppd_plugin) {
		free(dst->pppd_plugin);
		dst->pppd_plugin = src->pppd_plugin;
	}
	if (src->pppd_ipparam) {
		free(dst->pppd_ipparam);
		dst->pppd_ipparam = src->pppd_ipparam;
	}
	if (src->pppd_ifname) {
		free(dst->pppd_ifname);
		dst->pppd_ifname = src->pppd_ifname;
	}
	if (src->pppd_call) {
		free(dst->pppd_call);
		dst->pppd_call = src->pppd_call;
	}
	if (src->ca_file) {
		free(dst->ca_file);
		dst->ca_file = src->ca_file;
	}
	if (src->user_cert) {
		free(dst->user_cert);
		dst->user_cert = src->user_cert;
	}
	if (src->user_key) {
		free(dst->user_key);
		dst->user_key = src->user_key;
	}
	if (src->insecure_ssl != invalid_cfg.insecure_ssl)
		dst->insecure_ssl = src->insecure_ssl;
	if (src->cipher_list) {
		free(dst->cipher_list);
		dst->cipher_list = src->cipher_list;
	}
	if (src->cert_whitelist) {
		while (dst->cert_whitelist != NULL) {
			struct x509_digest *tmp = dst->cert_whitelist->next;
			free(dst->cert_whitelist);
			dst->cert_whitelist = tmp;
		}
		dst->cert_whitelist = src->cert_whitelist;
	}
}
