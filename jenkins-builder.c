///////////////////////////////////////////////////////////////////////////////
// NAME:            main.c
//
// AUTHOR:          Ethan D. Twardy <ethan.twardy@gmail.com>
//
// DESCRIPTION:     Entrypoint for the jenkins-builder application
//
// CREATED:         02/16/2022
//
// LAST EDITED:     02/16/2022
//
// Copyright 2022, Ethan D. Twardy
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
////

#include <argp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <json-c/json_tokener.h>
#include <json-c/json_object.h>

const char* argp_program_version = "jenkins-builder 0.1.0";
const char* argp_program_bug_address = "<ethan.twardy@gmail.com>";
static char doc[] = "Create and track changes to files with a single command";
static struct argp_option options[] = {
    {"credential-file", 'c', "FILE", 0,
     "Read user credentials from this JSON file", 0},
    {"jenkins-host", 'h', "HOST", 0, "Base URL of Jenkins", 0},
    { 0 },
};

typedef struct Arguments {
    const char* credentials;
    const char* jenkins_host;
} Arguments;

typedef struct Credentials {
    char* user;
    char* token;
} Credentials;

char* get_file_contents(const char* path) {
    struct stat file_stat = {0};
    int result = stat(path, &file_stat);
    if (0 != result) {
        fprintf(stderr, "Couldn't open credentials file: %s\n",
            strerror(errno));
        return NULL;
    }

    FILE* input_file = fopen(path, "rb");
    if (NULL == input_file) {
        fprintf(stderr, "Couldn't open credentials file: %s\n",
            strerror(errno));
        return NULL;
    }

    char* file_contents = malloc(file_stat.st_size + 1);
    if (NULL == file_contents) {
        fprintf(stderr, "Couldn't open credentials file: %s\n",
            strerror(errno));
        fclose(input_file);
        return NULL;
    }

    size_t bytes_read = fread(file_contents, 1, file_stat.st_size, input_file);
    fclose(input_file);
    if (bytes_read != file_stat.st_size) {
        fprintf(stderr, "Read size was not expected size: %s\n",
            strerror(errno));
        free(file_contents);
        return NULL;
    }

    return file_contents;
}

int get_user_credentials(Credentials* credentials, const char* string) {
    json_object* object = json_tokener_parse(string);
    if (NULL == object) {
        fprintf(stderr, "Credentials file doesn't contain valid JSON\n");
        return 1;
    }

    json_object* user = json_object_object_get(object, "user");
    if (NULL == user || !json_object_is_type(user, json_type_string)) {
        fprintf(stderr, "Credentials file is missing valid 'user' key\n");
        json_object_put(object);
        return 2;
    }

    json_object* token = json_object_object_get(object, "token");
    if (NULL == token || !json_object_is_type(token, json_type_string)) {
        fprintf(stderr, "Credentials file is missing valid 'token' key\n");
        json_object_put(object);
        return 3;
    }

    credentials->user = strdup(json_object_get_string(user));
    credentials->token = strdup(json_object_get_string(token));
    json_object_put(object);
    return 0;
}

void credentials_release(Credentials* credentials)
{ free(credentials->user); free(credentials->token); }

char* get_project_url_owned(const char* jenkins_url, const char* project) {
    const char* job = "/job/";
    const char* build = "/build";
    size_t url_length = strlen(jenkins_url) + strlen(job) + strlen(project)
        + strlen(build) + 1;
    char* url = malloc(url_length);
    if (NULL == url) {
        return NULL;
    }

    memset(url, 0, url_length);
    strcat(url, jenkins_url);
    strcat(url, job);
    strcat(url, project);
    strcat(url, build);
    return url;
}

int build_project(CURL* curl, const char* project_url, const char* project) {
    curl_easy_setopt(curl, CURLOPT_URL, project_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    /* curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); */
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");

    CURLcode result = curl_easy_perform(curl);
    if (CURLE_OK != result) {
        fprintf(stderr, "Couldn't build project '%s': %s\n", project,
            curl_easy_strerror(result));
        long result_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result_code);
        return result_code;
    }

    return 0;
}

static error_t parse_opt(int key, char* arg, struct argp_state* state) {
    Arguments* args = state->input;
    switch (key) {
    case ARGP_KEY_ARG:
        argp_usage(state);
        break;
    case ARGP_KEY_END:
        if (NULL == args->credentials) {
            fprintf(stderr, "A credentials file is required");
            argp_usage(state);
        } else if (NULL == args->jenkins_host) {
            fprintf(stderr, "A Jenkins host URL is required");
            argp_usage(state);
        }
        break;
    case 'h':
        args->jenkins_host = arg;
        break;
    case 'c':
        args->credentials = arg;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static struct argp argp = { options, parse_opt, 0, doc, 0, 0, 0 };
int main(int argc, char** argv) {
    Arguments arguments = {0};
    argp_parse(&argp, argc, argv, 0, 0, &arguments);
    char* env_projects = getenv("PROJECTS");
    if (NULL == env_projects) {
        fprintf(stderr, "PROJECTS is not set in the environment!\n");
        return EINVAL;
    }

    char* credentials_file = get_file_contents(arguments.credentials);
    if (NULL == credentials_file) {
        return EINVAL;
    }

    Credentials credentials = {0};
    int result = get_user_credentials(&credentials, credentials_file);
    free(credentials_file);
    if (0 != result) {
        return result;
    }

    CURL* curl = curl_easy_init();
    if (NULL == curl) {
        return ENOMEM;
    }

    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(curl, CURLOPT_USERNAME, credentials.user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, credentials.token);

    char* project = NULL;
    char* saveptr = NULL;
    for (project = strtok_r(env_projects, ":", &saveptr); project;
         project = strtok_r(NULL, ":", &saveptr))
    {
        char* project_url = get_project_url_owned(arguments.jenkins_host,
            project);
        result = build_project(curl, project_url, project);
        free(project_url);
        if (0 != result) {
            break;
        }
    }

    curl_easy_cleanup(curl);
    credentials_release(&credentials);
    return result;
}

///////////////////////////////////////////////////////////////////////////////
