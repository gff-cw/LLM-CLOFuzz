#define _GNU_SOURCE // asprintf
#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

#include "chat-llm.h"
#include "alloc-inl.h"
#include "hash.h"

// -lcurl -ljson-c -lpcre2-8
// apt install libcurl4-openssl-dev libjson-c-dev libpcre2-dev libpcre2-8-0

#define MAX_TOKENS 2048
#define CONFIDENT_TIMES 3

struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t chat_with_llm_helper(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL)
    {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static llm_config_t g_llm_config = {
    .model = DEFAULT_LLM_MODEL,
    .base_url = "https://api.openai.com/v1",
    .api_key = "",
    .timeout = 60,
    .max_tokens = 2048,
    .loaded = 0
};

int load_llm_config(const char *path)
{
    const char *candidate_paths[5] = {NULL};
    int count = 0;

    if (path && *path)
        candidate_paths[count++] = path;

    const char *env_cfg = getenv("LLM_CONFIG_FILE");
    if (!env_cfg || !*env_cfg) env_cfg = getenv("AFL_LLM_CONFIG");
    if (env_cfg && *env_cfg)
        candidate_paths[count++] = env_cfg;

    candidate_paths[count++] = "llm_config.json";
    candidate_paths[count++] = "../llm_config.json";
    candidate_paths[count++] = "llm_config.cfg";

    const char *found_path = NULL;
    for (int i = 0; i < count; i++)
    {
        if (candidate_paths[i] && access(candidate_paths[i], R_OK) == 0)
        {
            found_path = candidate_paths[i];
            break;
        }
    }

    if (!found_path)
    {
        const char *m = getenv("LLM_MODEL");
        if (!m || !*m) m = getenv("OPENAI_MODEL");
        if (m && *m)
        {
            strncpy(g_llm_config.model, m, sizeof(g_llm_config.model) - 1);
            g_llm_config.model[sizeof(g_llm_config.model) - 1] = '\0';
        }

        const char *u = getenv("OPENAI_BASE_URL");
        if (!u || !*u) u = getenv("OPENAI_API_BASE");
        if (!u || !*u) u = getenv("LLM_BASE_URL");
        if (u && *u)
        {
            strncpy(g_llm_config.base_url, u, sizeof(g_llm_config.base_url) - 1);
            g_llm_config.base_url[sizeof(g_llm_config.base_url) - 1] = '\0';
        }

        const char *k = getenv("OPENAI_API_KEY");
        if (!k || !*k) k = getenv("LLM_API_KEY");
        if (k && *k)
        {
            strncpy(g_llm_config.api_key, k, sizeof(g_llm_config.api_key) - 1);
            g_llm_config.api_key[sizeof(g_llm_config.api_key) - 1] = '\0';
        }

        g_llm_config.loaded = 1;
        printf("[*] Note: 'llm_config.json' not found in working directory; using defaults / env (Model: %s)\n", g_llm_config.model);
        return 0;
    }

    /* Try parsing as JSON first */
    json_object *root = json_object_from_file(found_path);
    if (root && json_object_is_type(root, json_type_object))
    {
        json_object *val = NULL;
        if (json_object_object_get_ex(root, "model", &val) && json_object_is_type(val, json_type_string))
        {
            strncpy(g_llm_config.model, json_object_get_string(val), sizeof(g_llm_config.model) - 1);
            g_llm_config.model[sizeof(g_llm_config.model) - 1] = '\0';
        }

        if (json_object_object_get_ex(root, "base_url", &val) && json_object_is_type(val, json_type_string))
        {
            strncpy(g_llm_config.base_url, json_object_get_string(val), sizeof(g_llm_config.base_url) - 1);
            g_llm_config.base_url[sizeof(g_llm_config.base_url) - 1] = '\0';
        }

        if (json_object_object_get_ex(root, "api_key", &val) && json_object_is_type(val, json_type_string))
        {
            strncpy(g_llm_config.api_key, json_object_get_string(val), sizeof(g_llm_config.api_key) - 1);
            g_llm_config.api_key[sizeof(g_llm_config.api_key) - 1] = '\0';
        }

        if (json_object_object_get_ex(root, "timeout", &val) && json_object_is_type(val, json_type_int))
        {
            g_llm_config.timeout = (long)json_object_get_int64(val);
        }

        if (json_object_object_get_ex(root, "max_tokens", &val) && json_object_is_type(val, json_type_int))
        {
            g_llm_config.max_tokens = json_object_get_int(val);
        }

        json_object_put(root);
        g_llm_config.loaded = 1;

        printf("[+] Loaded LLM configuration from '%s':\n", found_path);
        printf("    Model    : %s\n", g_llm_config.model);
        printf("    Base URL : %s\n", g_llm_config.base_url[0] ? g_llm_config.base_url : "(default)");
        printf("    API Key  : %s\n", (g_llm_config.api_key[0] && strcmp(g_llm_config.api_key, "YOUR_API_KEY_HERE") != 0) ? "[Configured in file]" : "[Unset / Template]");
        if (g_llm_config.timeout > 0)
            printf("    Timeout  : %ld seconds\n", g_llm_config.timeout);
        if (g_llm_config.max_tokens > 0)
            printf("    MaxTokens: %d\n", g_llm_config.max_tokens);
        return 1;
    }

    if (root) json_object_put(root);

    /* Secondary fallback: parse key = value lines */
    FILE *fp = fopen(found_path, "r");
    if (fp)
    {
        char line[1024];
        while (fgets(line, sizeof(line), fp))
        {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == ';' || *p == '\n' || *p == '\r' || *p == '\0') continue;

            char *eq = strchr(p, '=');
            if (!eq) eq = strchr(p, ':');
            if (eq)
            {
                *eq = '\0';
                char *k = p;
                char *v = eq + 1;
                char *kend = k + strlen(k) - 1;
                while (kend >= k && (*kend == ' ' || *kend == '\t' || *kend == '\"' || *kend == '\'')) *kend-- = '\0';
                while (*v == ' ' || *v == '\t' || *v == '\"' || *v == '\'') v++;
                char *vend = v + strlen(v) - 1;
                while (vend >= v && (*vend == ' ' || *vend == '\t' || *vend == '\r' || *vend == '\n' || *vend == '\"' || *vend == '\'')) *vend-- = '\0';

                if (strcasecmp(k, "model") == 0)
                    strncpy(g_llm_config.model, v, sizeof(g_llm_config.model) - 1);
                else if (strcasecmp(k, "base_url") == 0 || strcasecmp(k, "url") == 0)
                    strncpy(g_llm_config.base_url, v, sizeof(g_llm_config.base_url) - 1);
                else if (strcasecmp(k, "api_key") == 0 || strcasecmp(k, "key") == 0 || strcasecmp(k, "token") == 0)
                    strncpy(g_llm_config.api_key, v, sizeof(g_llm_config.api_key) - 1);
                else if (strcasecmp(k, "timeout") == 0)
                    g_llm_config.timeout = atol(v);
                else if (strcasecmp(k, "max_tokens") == 0)
                    g_llm_config.max_tokens = atoi(v);
            }
        }
        fclose(fp);
        g_llm_config.loaded = 1;
        printf("[+] Loaded LLM config (text format) from '%s':\n", found_path);
        printf("    Model    : %s\n", g_llm_config.model);
        printf("    Base URL : %s\n", g_llm_config.base_url[0] ? g_llm_config.base_url : "(default)");
        printf("    API Key  : %s\n", (g_llm_config.api_key[0] && strcmp(g_llm_config.api_key, "YOUR_API_KEY_HERE") != 0) ? "[Configured in file]" : "[Unset / Template]");
        return 1;
    }

    g_llm_config.loaded = 1;
    return 0;
}

const llm_config_t *get_llm_config(void)
{
    if (!g_llm_config.loaded)
        load_llm_config(NULL);
    return &g_llm_config;
}

void set_target_llm_model(const char *model)
{
    if (model && *model)
    {
        strncpy(g_llm_config.model, model, sizeof(g_llm_config.model) - 1);
        g_llm_config.model[sizeof(g_llm_config.model) - 1] = '\0';
    }
}

const char *get_target_llm_model(void)
{
    const llm_config_t *cfg = get_llm_config();
    if (cfg->model[0])
        return cfg->model;
    return DEFAULT_LLM_MODEL;
}

const char *get_target_llm_base_url(void)
{
    const llm_config_t *cfg = get_llm_config();
    if (cfg->base_url[0])
        return cfg->base_url;
    return "https://api.openai.com/v1";
}

const char *get_target_llm_api_key(void)
{
    const llm_config_t *cfg = get_llm_config();
    if (cfg->api_key[0])
        return cfg->api_key;
    return "";
}

char *chat_with_llm(char *prompt, char *model, int tries, float temperature)
{
    if (!prompt) return NULL;

    const llm_config_t *cfg = get_llm_config();

    const char *active_model = NULL;
    if (model && strcmp(model, "turbo") != 0 && strcmp(model, "instruct") != 0)
        active_model = model;
    else
        active_model = get_target_llm_model();

    int is_legacy_instruct = 0;
    if (strcmp(active_model, "gpt-3.5-turbo-instruct") == 0 ||
        (strstr(active_model, "instruct") && !strstr(active_model, "chat")))
    {
        is_legacy_instruct = 1;
    }

    char default_url[512];
    char *url = default_url;
    const char *custom_base = NULL;

    const char *endpoint = getenv("LLM_ENDPOINT");
    if (endpoint && *endpoint)
    {
        url = (char *)endpoint;
    }
    else
    {
        custom_base = (cfg->base_url[0] && strcmp(cfg->base_url, "https://api.openai.com/v1") != 0) ? cfg->base_url : getenv("OPENAI_BASE_URL");
        if (!custom_base || !*custom_base) custom_base = getenv("OPENAI_API_BASE");
        if (!custom_base || !*custom_base) custom_base = getenv("LLM_BASE_URL");
        if (!custom_base || !*custom_base) custom_base = cfg->base_url;

        if (custom_base && *custom_base)
        {
            char clean_base[256];
            strncpy(clean_base, custom_base, sizeof(clean_base) - 1);
            clean_base[sizeof(clean_base) - 1] = '\0';
            size_t blen = strlen(clean_base);
            while (blen > 0 && clean_base[blen - 1] == '/')
                clean_base[--blen] = '\0';

            if (is_legacy_instruct)
                snprintf(default_url, sizeof(default_url), "%s/completions", clean_base);
            else
                snprintf(default_url, sizeof(default_url), "%s/chat/completions", clean_base);
        }
        else
        {
            if (is_legacy_instruct)
                url = "https://api.openai.com/v1/completions";
            else
                url = "https://api.openai.com/v1/chat/completions";
        }
    }

    const char *token = (cfg->api_key[0] && strcmp(cfg->api_key, "YOUR_API_KEY_HERE") != 0) ? cfg->api_key : getenv("OPENAI_API_KEY");
    if (!token || !*token) token = getenv("LLM_API_KEY");
    if (!token || !*token) token = getenv("OPENAI_TOKEN");

    char auth_header[512] = {0};
    if (token && *token && strcmp(token, "1") != 0 && strcmp(token, "YOUR_API_KEY_HERE") != 0)
    {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    }
    else if (!custom_base && !getenv("LLM_ENDPOINT"))
    {
        static int warned_token = 0;
        if (!warned_token)
        {
            printf("[-] Warning: No valid API Key configured in 'llm_config.json' or OPENAI_API_KEY. Skipping LLM request.\n");
            warned_token = 1;
        }
        return NULL;
    }

    int max_tokens = cfg->max_tokens > 0 ? cfg->max_tokens : MAX_TOKENS;
    const char *env_max_tokens = getenv("LLM_MAX_TOKENS");
    if (env_max_tokens && atoi(env_max_tokens) > 0)
        max_tokens = atoi(env_max_tokens);

    json_object *request_obj = json_object_new_object();
    json_object_object_add(request_obj, "model", json_object_new_string(active_model));
    json_object_object_add(request_obj, "max_tokens", json_object_new_int(max_tokens));
    json_object_object_add(request_obj, "temperature", json_object_new_double(temperature));

    if (is_legacy_instruct)
    {
        json_object_object_add(request_obj, "prompt", json_object_new_string(prompt));
    }
    else
    {
        json_object *messages_obj = NULL;
        if (prompt[0] == '[')
        {
            messages_obj = json_tokener_parse(prompt);
            if (!messages_obj || !json_object_is_type(messages_obj, json_type_array))
            {
                if (messages_obj) json_object_put(messages_obj);
                messages_obj = NULL;
            }
        }

        if (!messages_obj)
        {
            messages_obj = json_object_new_array();
            json_object *sys_msg = json_object_new_object();
            json_object_object_add(sys_msg, "role", json_object_new_string("system"));
            json_object_object_add(sys_msg, "content", json_object_new_string("You are a helpful assistant for network protocol fuzzing and message format extraction."));
            json_object_array_add(messages_obj, sys_msg);

            json_object *user_msg = json_object_new_object();
            json_object_object_add(user_msg, "role", json_object_new_string("user"));
            json_object_object_add(user_msg, "content", json_object_new_string(prompt));
            json_object_array_add(messages_obj, user_msg);
        }

        json_object_object_add(request_obj, "messages", messages_obj);
    }

    const char *json_payload = json_object_to_json_string(request_obj);
    char *data = strdup(json_payload ? json_payload : "{}");
    json_object_put(request_obj);

    long timeout_sec = cfg->timeout > 0 ? cfg->timeout : 60L;
    const char *env_timeout = getenv("LLM_TIMEOUT");
    if (env_timeout && atol(env_timeout) > 0)
        timeout_sec = atol(env_timeout);

    long connect_timeout_sec = 15L;
    const char *env_connect_timeout = getenv("LLM_CONNECT_TIMEOUT");
    if (env_connect_timeout && atol(env_connect_timeout) > 0)
        connect_timeout_sec = atol(env_connect_timeout);

    CURL *curl;
    CURLcode res = CURLE_OK;
    char *answer = NULL;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    do
    {
        struct MemoryStruct chunk;
        chunk.memory = malloc(1);
        chunk.size = 0;

        curl = curl_easy_init();
        if (curl)
        {
            struct curl_slist *headers = NULL;
            if (auth_header[0])
                headers = curl_slist_append(headers, auth_header);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: application/json");

            const char *org_id = getenv("OPENAI_ORG_ID");
            if (org_id && *org_id)
            {
                char org_header[256];
                snprintf(org_header, sizeof(org_header), "OpenAI-Organization: %s", org_id);
                headers = curl_slist_append(headers, org_header);
            }
            const char *project_id = getenv("OPENAI_PROJECT_ID");
            if (project_id && *project_id)
            {
                char proj_header[256];
                snprintf(proj_header, sizeof(proj_header), "OpenAI-Project: %s", project_id);
                headers = curl_slist_append(headers, proj_header);
            }

            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_sec);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_with_llm_helper);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

            res = curl_easy_perform(curl);

            if (res == CURLE_OK)
            {
                json_object *jobj = json_tokener_parse(chunk.memory);
                if (jobj)
                {
                    json_object *err_obj = NULL;
                    if (json_object_object_get_ex(jobj, "error", &err_obj))
                    {
                        const char *err_str = json_object_to_json_string(err_obj);
                        printf("[-] LLM API error [%s]: %s\n", active_model, err_str ? err_str : "Unknown");
                    }

                    json_object *choices = NULL;
                    if (json_object_object_get_ex(jobj, "choices", &choices) &&
                        json_object_is_type(choices, json_type_array) &&
                        json_object_array_length(choices) > 0)
                    {
                        json_object *first_choice = json_object_array_get_idx(choices, 0);
                        const char *data_str = NULL;

                        json_object *msg_obj = NULL;
                        json_object *content_obj = NULL;
                        json_object *text_obj = NULL;

                        if (json_object_object_get_ex(first_choice, "message", &msg_obj) &&
                            json_object_object_get_ex(msg_obj, "content", &content_obj))
                        {
                            data_str = json_object_get_string(content_obj);
                        }
                        else if (json_object_object_get_ex(first_choice, "text", &text_obj))
                        {
                            data_str = json_object_get_string(text_obj);
                        }

                        if (data_str)
                        {
                            if (data_str[0] == '\n')
                                data_str++;
                            answer = strdup(data_str);
                        }
                    }
                    json_object_put(jobj);
                }
                else
                {
                    printf("[-] Failed to parse LLM JSON response for model %s: %s\n", active_model, chunk.memory ? chunk.memory : "NULL");
                    sleep(2);
                }
            }
            else
            {
                printf("[-] CURL error contacting LLM (%s): %s\n", url, curl_easy_strerror(res));
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }

        free(chunk.memory);
    } while ((res != CURLE_OK || answer == NULL) && (--tries > 0));

    if (data != NULL)
    {
        free(data);
    }

    curl_global_cleanup();
    return answer;
}

char *construct_prompt_stall(char *protocol_name, char *examples, char *history)
{
    char *template = "In the %s protocol, the communication history between the %s client and the %s server is as follows."
                     "The next proper client request that can affect the server's state are:\\n\\n"
                     "Desired format of real client requests:\\n%sCommunication History:\\n\\\"\\\"\\\"\\n%s\\\"\\\"\\\"";

    char *prompt = NULL;
    asprintf(&prompt, template, protocol_name, protocol_name, protocol_name, examples, history);

    char *final_prompt = NULL;

    asprintf(&final_prompt, "[{\"role\": \"system\", \"content\": \"You are a helpful assistant.\"}, {\"role\": \"user\", \"content\": \"%s\"}]", prompt);

    free(prompt);

    return final_prompt;
}

char *construct_prompt_for_templates(char *protocol_name, char **final_msg)
{
    // Give one example for learning formats
    char *prompt_rtsp_example = "For the RTSP protocol, the DESCRIBE client request template is:\\n"
                                "DESCRIBE: [\\\"DESCRIBE <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"CSeq: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"User-Agent: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"Accept: <<VALUE>>\\\\r\\\\n\\\","
                                "\\\"\\\\r\\\\n\\\"]";

    char *prompt_http_example = "For the HTTP protocol, the GET client request template is:\\n"
                                "GET: [\\\"GET <<VALUE>>\\\\r\\\\n\\\"]";

    char *msg = NULL;
    asprintf(&msg, "%s\\n%s\\nFor the %s protocol, all of client request templates are :", prompt_rtsp_example, prompt_http_example, protocol_name);
    *final_msg = msg;
    /** Format of prompt_grammars
    prompt_grammars = [
        {"role": "system", "content": "You are a helpful assistant."},
        {"role": "user", "content": msg}
    ]
     **/
    char *prompt_grammars = NULL;

    asprintf(&prompt_grammars, "[{\"role\": \"system\", \"content\": \"You are a helpful assistant.\"}, {\"role\": \"user\", \"content\": \"%s\"}]", msg);

    return prompt_grammars;
}

char *construct_prompt_for_remaining_templates(char *protocol_name, char *first_question, char *first_answer)
{
    char *second_question = NULL;
    asprintf(&second_question, "For the %s protocol, other templates of client requests are:", protocol_name);

    json_object *answer_str = json_object_new_string(first_answer);
    // printf("The First Question\n%s\n\n", first_question);
    // printf("The First Answer\n%s\n\n", first_answer);
    // printf("The Second Question\n%s\n\n", second_question);
    const char *answer_str_escaped = json_object_to_json_string(answer_str);

    char *prompt = NULL;

    asprintf(&prompt,
             "["
             "{\"role\": \"system\", \"content\": \"You are a helpful assistant.\"},"
             "{\"role\": \"user\", \"content\": \"%s\"},"
             "{\"role\": \"assistant\", \"content\": %s },"
             "{\"role\": \"user\", \"content\": \"%s\"}"
             "]",
             first_question, answer_str_escaped, second_question);

    json_object_put(answer_str);
    free(second_question);

    return prompt;
}

char *extract_stalled_message(char *message, size_t message_len)
{

    int errornumber;
    size_t erroroffset;
    // After a lot of iterations, the model consistently responds with an empty line and then a line of text
    pcre2_code *extracter = pcre2_compile("\r?\n?.*?\r?\n", PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL);
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(extracter, NULL);
    int rc = pcre2_match(extracter, message, message_len, 0, 0, match_data, NULL);
    char *res = NULL;
    if (rc >= 0)
    {
        size_t *ovector = pcre2_get_ovector_pointer(match_data);
        res = strdup(message + ovector[1]);
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(extracter);

    return res;
}

char *format_request_message(char *message)
{

    int message_len = strlen(message);
    int max_len = message_len;
    int res_len = 0;
    char *res = ck_alloc(message_len * sizeof(char));
    for (int i = 0; i < message_len; i++)
    {
        // If an \n is not padded with an \r before, we add it
        if (message[i] == '\n' && (i == 0 || (message[i - 1] != '\r')))
        {
            if (res_len == max_len)
            {
                res = ck_realloc(res, max_len + 10);
                max_len += 10;
            }
            res[res_len++] = '\r';
        }

        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = message[i];
    }

    // Add \r\n\r\n to ensure that the packet is accepted
    for (int i = 0; i < 2; i++)
    {
        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = '\r';
        if (res_len == max_len)
        {
            res = ck_realloc(res, max_len + 10);
            max_len += 10;
        }
        res[res_len++] = '\n';
    }

    if (res_len == max_len)
    {
        res = ck_realloc(res, max_len + 1);
        max_len++;
    }
    res[res_len++] = '\0';
    free(message);
    return res;
}

char *construct_prompt_for_protocol_message_types(char *protocol_name)
{
    /***
     * Prompt to ask the protocol states as follow:
     * ```
     * In the RTSP protocol, the protocol states are:
     *
     * Desired format:
     * <comma_separated_list_of_states_in_uppercase>
     * ```
     * ***/
    char *prompt = NULL;

    // transfer the prompt into string
    asprintf(&prompt, "In the %s protocol, the message types are: \\n\\nDesired format:\\n<comma_separated_list_of_states_in_uppercase_and_without_whitespaces>", protocol_name);

    return prompt;
}

char *construct_prompt_for_requests_to_states(const char *protocol_name,
                                              const char *protocol_state,
                                              const char *example_requests)
{
    /***
     Prompt to ask the sequence of client requests to reach a protocol state as follows:
        ```
        In the RTSP protocol, if the server just starts, to reach the PLAYING state, the sequence of client requests can be:
        DESCRIBE rtsp://127.0.0.1:8554/aacAudioTest RTSP/1.0
        CSeq: 2
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Accept: application/sdp

        SETUP rtsp://127.0.0.1:8554/aacAudioTest/track1 RTSP/1.0
        CSeq: 3
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Transport: RTP/AVP;unicast;client_port=38784-38785

        PLAY rtsp://127.0.0.1:8554/aacAudioTest/ RTSP/1.0
        CSeq: 4
        User-Agent: ./testRTSPClient (LIVE555 Streaming Media v2018.08.28)
        Session: 000022B8
        Range: npt=0.000-

        Similarly, in the RTSP protocol, if the server just starts, to reach the RECORD state, the sequence of client requests can be:
     ***/

    // Transfer formats of example_requests
    json_object *example_requests_json = json_object_new_string(example_requests);
    const char *example_requests_json_str = json_object_to_json_string(example_requests_json);

    json_object *protocol_state_json = json_object_new_string(protocol_state);
    const char *protocol_state_json_str = json_object_to_json_string(protocol_state_json);

    char *prompt = NULL;

    int example_request_len = strlen(example_requests_json_str) - 2;
    if (example_request_len > EXAMPLE_SEQUENCE_PROMPT_LENGTH)
    {
        example_request_len = EXAMPLE_SEQUENCE_PROMPT_LENGTH;
    }

    asprintf(&prompt,
             "In the %s protocol, if the server just starts, to reach the INIT state, the sequence of client requests can be:\\n"
             "%.*s\\nSimilarly, in the %s protocol, if the server just starts, to reach the %.*s state, the sequence of client requests can be:\\n",
             protocol_name,
             example_request_len,
             example_requests_json_str + 1,
             protocol_name,
             (int)strlen(protocol_state_json_str) - 2,
             protocol_state_json_str + 1);

    json_object_put(protocol_state_json);
    json_object_put(example_requests_json);

    return prompt;
}

void extract_message_grammars(char *answers, klist_t(gram) * grammar_list)
{

    char *ptr = answers;
    int len = strlen(answers);

    while (ptr < answers + len)
    {
        char *start = strchr(ptr, '[');
        if (start == NULL)
            break;
        char *end = strchr(start, ']');
        if (end == NULL)
            break;
        int count = end - start + 1;
        char *temp = (char *)ck_alloc(count + 1);
        strncpy(temp, start, count);
        temp[count] = '\0';
        ptr = end + 1;

        // conver temp to json object and save it to the list
        json_object *jobj = json_tokener_parse(temp);
        *kl_pushp(gram, grammar_list) = jobj;

        // printf("%s\n", temp);
    }
}

int parse_pattern(pcre2_code *replacer, pcre2_match_data *match_data, const char *str, size_t len, char *pattern)
{
    strcat(pattern, "(?:");
    // offset == 3;
    int rc = pcre2_match(replacer, str, len, 0, 0, match_data, NULL);

    if (rc < 0)
    {
        switch (rc)
        {
        case PCRE2_ERROR_NOMATCH:
            // printf("No match for %s!\n", str);
            break;
        default:
            // printf("Matching error %d\n", rc);
            break;
        }
        pcre2_match_data_free(match_data);
        pcre2_code_free(replacer);
        return 0;
    }
    // printf("RC is %d\n",rc);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    // for(int i = 1; i<rc;i++){
    //     printf("Start %d, end %d\n",ovector[2*i],ovector[2*i+1]);
    // }

    if (rc == 4)
    { // matched the first option - there is a special value
        strncat(pattern, str + ovector[2], ovector[3] - ovector[2]);
        // offset += ovector[3] - ovector[2];

        strcat(pattern, "(.*)");
        // offset += 3;

        strncat(pattern, str + ovector[6], ovector[7] - ovector[6]);
        // offset += ovector[7] - ovector[6];
    }
    else if (rc == 5)
    {
        // matched the second option - there is no special value
        strncat(pattern, str + ovector[8], ovector[9] - ovector[8]);
        // offset += ovector[9] - ovector[8];
    }
    else
    {
        FATAL("Regex groups were updated but not the handling code.");
    }
    strcat(pattern, ")");
    return 1;
}

// If successful, puts 2 patterns in the patterns array, the first one is the header, the second is the fields
// Else returns an array with the first element being NULL
char *extract_message_pattern(const char *header_str, khash_t(field_table) * field_table, pcre2_code **patterns, int debug_file, const char *debug_file_name)
{
    int errornumber;
    size_t erroroffset;
    char header_pattern[128] = {0};
    char fields_pattern[1024] = {0};
    pcre2_code *replacer = pcre2_compile("(?:(.*)(?:<<(.*)>>)(.*))|(.+)", PCRE2_ZERO_TERMINATED, PCRE2_DOTALL, &errornumber, &erroroffset, NULL);
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(replacer, NULL);
    char *message_type = NULL;
    // int offset = 0;
    /**
     * Example output
     * patterns[0] = (?:PLAY (.*)\r\n)
     * patterns[1] = (?|(?:CSeq: (.*)\r\n)|(?:User-Agent: (.*)\r\n)|(?:Range: (.*)\r\n)|(?:\r\n))
     */

    {
        // We use the string in such an escaped format for easier debugging as the regex library supports parsing it properly
        // The string contains quotations so they are ignored
        header_str++;

        int message_len = 0;
        while (header_str[message_len] != '\0' 
        && header_str[message_len] != ' ' 
        && header_str[message_len] != '\n' 
        && header_str[message_len] != '\r' 
        && header_str[message_len] != '\\' )
        {
            message_len++;
        }
        message_type = ck_alloc(message_len + 1);
        memcpy(message_type, header_str, message_len);
        message_type[message_len] = '\0';

        size_t len = strlen(header_str) - 1;
        strcat(header_pattern, "^"); // Ensure that it captures the start of the string
        if (!parse_pattern(replacer, match_data, header_str, len, header_pattern))
        {
            patterns[0] = NULL;
            return NULL;
        }
    }

    int first = 1;

    strcat(fields_pattern, "(?|");
    for (khiter_t field_t_iter = kh_begin(field_table); field_t_iter != kh_end(field_table); ++field_t_iter)
    {
        if (!kh_exist(field_table, field_t_iter) || kh_value(field_table, field_t_iter) < (TEMPLATE_CONSISTENCY_COUNT / 2 + (TEMPLATE_CONSISTENCY_COUNT % 2)))
            continue;

        if (!first)
        {
            strcat(fields_pattern, "|");
        }
        else
        {
            first = 0;
        }

        json_object *field_v = json_object_new_string(kh_key(field_table, field_t_iter));
        const char *str = json_object_to_json_string(field_v);
        // We use the string in such an escaped format for easier debugging as the regex library supports parsing it properly
        // The string contains quotations so they are ignored
        str++;
        size_t len = strlen(str) - 1;
        int matched = parse_pattern(replacer, match_data, str, len, fields_pattern);
        json_object_put(field_v);
        if (!matched)
        {
            patterns[0] = NULL;
            return NULL;
        }
    }

    strcat(fields_pattern, ")");

    if (first == 1)
    { // convert from (?|) to (.+) when the group is empty
        fields_pattern[1] = '.';
        fields_pattern[2] = '+';
    }

    pcre2_match_data_free(match_data);
    pcre2_code_free(replacer);
    printf("Header pattern is %s\n", header_pattern);
    printf("Fields pattern is %s\n", fields_pattern);

    if (debug_file != -1 && debug_file_name != NULL)
    {
        ck_write(debug_file, header_pattern, strlen(header_pattern), debug_file_name);
        ck_write(debug_file, "\n", 1, debug_file_name);
        ck_write(debug_file, fields_pattern, strlen(fields_pattern), debug_file_name);
    }

    {
        pcre2_code *p = pcre2_compile(header_pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL);
        pcre2_jit_compile(p, PCRE2_JIT_COMPLETE);
        patterns[0] = p;
    }
    {
        pcre2_code *p = pcre2_compile(fields_pattern, PCRE2_ZERO_TERMINATED, 0, &errornumber, &erroroffset, NULL);
        pcre2_jit_compile(p, PCRE2_JIT_COMPLETE);
        patterns[1] = p;
    }
    return message_type;
}

range_list starts_with(char *line, int length, pcre2_code *pattern)
{
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(pattern, NULL);

    int rc = pcre2_match(pattern, line, length, 0, 0, match_data, NULL); // find the first range

    // printf("starts_with rc is %d\n", rc);
    if (rc < 0)
    {
        switch (rc)
        {
        case PCRE2_ERROR_NOMATCH:
            // printf("No match!\n");
            break;
        default:
            // printf("Matching error %d\n", rc);
            break;
        }
        pcre2_match_data_free(match_data);
        range_list res;
        kv_init(res);
        return res;
    }

    range_list dyn_ranges;
    kv_init(dyn_ranges);
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    for (int i = 1; i < rc; i++)
    {
        if (ovector[2 * i] == -1)
            continue;
        // printf("Group %d %d %d\n",i, ovector[2 * i], ovector[2 * i + 1]);
        range v = {.start = ovector[2 * i], .len = ovector[2 * i + 1] - ovector[2 * i], .mutable = 1};
        kv_push(range, dyn_ranges, v);
        // kv_push(range, dyn_ranges, v);
        //  ranges[0][i - 1] = v;
    }
    range v = {.start = ovector[0], .len = ovector[1] - ovector[0], .mutable = 1};
    kv_push(range, dyn_ranges, v); // add the global range at the end

    pcre2_match_data_free(match_data);
    return dyn_ranges;
}

range_list get_mutable_ranges(char *line, int length, int offset, pcre2_code *pattern)
{
    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(pattern, NULL);

    range_list dyn_ranges;
    kv_init(dyn_ranges);

    for (;;) // catch all the other ranges
    {
        int rc = pcre2_match(pattern, line, length, offset, 0, match_data, NULL);
        if (rc < 0)
        {
            switch (rc)
            {
            case PCRE2_ERROR_NOMATCH:
                // printf("No match!\n");
                break;
            default:
                // printf("Matching error %d\n", rc);
                break;
            }
            pcre2_match_data_free(match_data);
            match_data = NULL;
            break;
        }
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
        if (offset != ovector[0])
        {
            range v = {.start = offset, .len = ovector[0] - offset, .mutable = 1};
            kv_push(range, dyn_ranges, v);
        }

        // printf("Matched over %d %d\n", ovector[0], ovector[1]);
        for (int i = 1; i < rc; i++)
        {
            if (ovector[2 * i] == -1)
                continue;
            // printf("Group %d %d %d\n",i, ovector[2 * i], ovector[2 * i + 1]);
            range v = {.start = ovector[2 * i], .len = ovector[2 * i + 1] - ovector[2 * i], .mutable = 1};
            kv_push(range, dyn_ranges, v);
            // ranges[0][i - 1] = v;
        }
        if (offset == ovector[1])
        { // in the case the match is empty, we just move a step forward
            offset++;
        }
        else
        {
            offset = ovector[1];
        }
    }

    if (offset < length) // catch anything past the last matched pattern
    {
        range v = {.start = offset, .len = length - offset, .mutable = 1};
        kv_push(range, dyn_ranges, v);
    }

    if (match_data != NULL)
    {
        pcre2_match_data_free(match_data);
    }
    return dyn_ranges;
}

char *unescape_string(const char *input)
{
    size_t length = strlen(input);
    char *output = (char *)malloc((length + 1) * sizeof(char));

    if (output == NULL)
    {
        printf("Memory allocation failed.\n");
        return NULL;
    }

    size_t i, j = 0;
    for (i = 0; i < length; i++)
    {
        if (input[i] == '\\')
        {
            i++; // Skip the backslash
            switch (input[i])
            {
            case 'n':
                output[j++] = '\n';
                break;
            case 't':
                output[j++] = '\t';
                break;
            case 'r':
                output[j++] = '\r';
                break;
            case '\\':
                output[j++] = '\\';
                break;
            default:
                output[j++] = input[i];
                break;
            }
        }
        else
        {
            output[j++] = input[i];
        }
    }

    output[j] = '\0'; // Add null-terminator to the output string
    return output;
}

void write_new_seeds(char *enriched_file, char *contents)
{
    FILE *fp = fopen(enriched_file, "w");
    if (fp == NULL)
    {
        printf("Error in opening the file %s\n", enriched_file);
        exit(1);
    }

    // remove the newline and whiltespace in the beginning of the string if any
    while (contents[0] == '\n' || contents[0] == ' ' || contents[0] == '\t' || contents[0] == '\r')
    {
        contents++;
    }

    // Check if last 4 characters of the client_request_answer string are \r\n\r\n
    // If not, add them
    int len = strlen(contents);
    if (len < 4 || contents[len - 1] != '\n' || contents[len - 2] != '\r' || contents[len - 3] != '\n' || contents[len - 4] != '\r')
    {
        fprintf(fp, "%s\r\n\r\n", contents);
    }
    else
    {
        fprintf(fp, "%s", contents);
    }

    fclose(fp);
}

char *format_string(char *state_string)
{
    // remove the newline and whiltespace in the beginning of the string if any
    while (state_string[0] == '\n' || state_string[0] == ' ' || state_string[0] == '\t' || state_string[0] == '\r')
    {
        state_string++;
    }

    int len = strlen(state_string);
    while (len > 0 && (state_string[len - 1] == '\n' || state_string[len - 1] == '\r' || state_string[len - 1] == ' ' || state_string[len - 1] == '.'))
    {
        state_string[len - 1] = '\0';
        len--;
    }

    return state_string;
}

/***
 * Get the protocol states based on self-consistency check
 * pass the parameters: protocol_name, states_set, states_string
 ***/
void get_protocol_message_types(char *state_prompt, khash_t(strSet) * states_set)
{
    khash_t(strMap) *state_to_times = kh_init(strMap); // map from state to times

    for (int i = 0; i < CONFIDENT_TIMES; i++)
    {
        char *state_answer = chat_with_llm(state_prompt, "instruct", MESSAGE_TYPE_RETRIES, 0.5);
        if (state_answer == NULL)
            continue;
        // printf("## Answer from LLM:\n %s\n", state_answer);

        state_answer = format_string(state_answer);

        char *state_tokens = strtok(state_answer, ",");
        while (state_tokens != NULL)
        {
            char *protocol_state = state_tokens;
            protocol_state = format_string(protocol_state);
            // save the state to the map
            int ret;
            khiter_t k = kh_put(strMap, state_to_times, protocol_state, &ret);
            if (ret == 0)
            {
                kh_value(state_to_times, k)++;
            }
            else
            {
                kh_value(state_to_times, k) = 1;
            }

            state_tokens = strtok(NULL, ",");
        }
    }

    // traverse the map and get the states whose times are larger than 0.5 * CONFIDENT_TIMES
    for (khiter_t k = kh_begin(state_to_times); k != kh_end(state_to_times); ++k)
    {
        if (kh_exist(state_to_times, k))
        {
            if (kh_value(state_to_times, k) >= 0.5 * CONFIDENT_TIMES)
            {
                const char *protocol_state = kh_key(state_to_times, k);
                // add the state to the set
                int ret;
                kh_put(strSet, states_set, protocol_state, &ret);
            }
        }
    }
}

khash_t(strSet) * duplicate_hash(khash_t(strSet) * set)
{
    khash_t(strSet) *new_set = kh_init(strSet);
    if (!set) return new_set;

    for (khiter_t k = kh_begin(set); k != kh_end(set); ++k)
    {
        if (kh_exist(set, k))
        {
            const char *val = kh_key(set, k);
            int ret;
            kh_put(strSet, new_set, val, &ret);
        }
    }

    return new_set;
}

// message_set_list generate_combinations(khash_t(strSet)* sequence, int size)
// {
//     if(size == 0)
//     {
//         message_set_list output;
//         kv_init(output);
//         kv_push(khash_t(strSet)*,output,kh_init(strSet));
//         return output;
//     }
//     else
//     {
//         message_set_list subcombinations = generate_combinations(sequence,size-1);
//         message_set_list newCombinations;
//         kv_init(newCombinations);
//         for(int i = 0; i < kv_size(subcombinations);i++)
//         {
//             khash_t(strSet)* target = kv_A(subcombinations,i);
//             khiter_t sequence_iter;
//             for (sequence_iter = kh_begin(sequence); sequence_iter != kh_end(sequence); sequence_iter++)
//             {
//                 if (!kh_exist(sequence, sequence_iter))
//                     continue;
//                 khiter_t k = kh_get(strSet, target, kh_val(sequence,sequence_iter));
//                 if (kh_exist(target, k))
//                     continue;
//                 khash_t(strSet)* newCombination = duplicate_hash(target);
//                 int absent;
//                 kh_put(strSet,newCombination,kh_val(sequence,sequence_iter))    
//             }
//         }
//         return newCombinations;
//     }
// }
void make_combination(khash_t(strSet)* sequence, char** data , message_set_list* res,khiter_t st, khiter_t end, int index, int size);

message_set_list message_combinations(khash_t(strSet)* sequence, int size)
{
    message_set_list res;
    kv_init(res);
    char* data[size];
    make_combination(sequence,data, &res, kh_begin(sequence), kh_end(sequence), 0, size);
    return res;
}

void make_combination(khash_t(strSet)* sequence, char** data , message_set_list* res,khiter_t st, khiter_t end,
                     int index, int size)
{

    if (index == size)
    {
        khash_t(strSet)* combination = kh_init(strSet);
        int absent;
        for (int j=0; j<size; j++){
            kh_put(strSet,combination, data[j],&absent );
        }
        kv_push(khash_t(strSet)*,*res,combination);
        return;
    }
    for (khiter_t i=st; i != end && end-i+1 >= size-index; i++)
    {
        if(!kh_exist(sequence,i))
            continue;
        data[index] = kh_key(sequence,i);
        make_combination(sequence, data,res, i+1, end, index+1, size);
    }
}



int min(int a, int b) {
    return a < b ? a : b;
}

char *enrich_sequence(char *sequence, khash_t(strSet) * missing_message_types)
{
    const char *prompt_template =
        "The following is one sequence of client requests:\\n"
        "%.*s\\n"
        "Please add the %.*s client requests in the proper locations, and the modified sequence of client requests is:";

    int missing_fields_len = 0;
    int missing_fields_capacity = 100;
    char *missing_fields_seq = ck_alloc(missing_fields_capacity);

    khiter_t k;
    int i = 0;
    for (k = kh_begin(missing_message_types); 
    k != kh_end(missing_message_types) && i < min(MAX_ENRICHMENT_MESSAGE_TYPES, kh_size(missing_message_types)); 
    ++k)
    {
        if (!kh_exist(missing_message_types, k))
            continue;
        ++i; // Increment only after seeing a message type
        const char *message_type = kh_key(missing_message_types, k);
        int needed_len = strlen(message_type) + 2; // add for the ', '

        if (missing_fields_len + needed_len > missing_fields_capacity)
        {
            missing_fields_capacity += 2 * needed_len;
            missing_fields_seq = ck_realloc(missing_fields_seq, missing_fields_capacity);
        }

        memcpy(missing_fields_seq + missing_fields_len, message_type, strlen(message_type));
        memcpy(missing_fields_seq + missing_fields_len + needed_len - 2, ", ", 2);

        missing_fields_len += needed_len;
    }
    if (missing_fields_len >= 2)
        missing_fields_len -= 2; // ignore the last ', '

    char *prompt = NULL;

    json_object *sequence_escaped = json_object_new_string(sequence);
    const char *sequence_escaped_str = json_object_to_json_string(sequence_escaped);
    if (sequence_escaped_str && *sequence_escaped_str == '"')
        sequence_escaped_str++;

    int sequence_len = strlen(sequence_escaped_str);
    if (sequence_len > 0 && sequence_escaped_str[sequence_len - 1] == '"')
        sequence_len--;
    int ctx_limit = MAX_TOKENS;
    const char *env_ctx = getenv("LLM_MAX_TOKENS");
    if (env_ctx && atoi(env_ctx) > 0) ctx_limit = atoi(env_ctx);

    int allowed_tokens = (ctx_limit - strlen(prompt_template) - missing_fields_len);
    if (sequence_len > allowed_tokens)
    {
        sequence_len = allowed_tokens;
    }
    asprintf(&prompt, prompt_template, sequence_len, sequence_escaped_str, missing_fields_len, missing_fields_seq);
    ck_free(missing_fields_seq);
    json_object_put(sequence_escaped);

    char *response = chat_with_llm(prompt, "instruct", ENRICHMENT_RETRIES, 0.5);

    free(prompt);

    return response;
}

// // For debugging
// // gcc -g -o chat-llm chat-llm.c chat-llm.h -lcurl -ljson-c -lpcre2-8
// int main(int argc, char **argv)
// {
//     char *protocol_name = argv[1];
//     char *in_dir = argv[2];
//     khash_t(strSet) *states_set = kh_init(strSet);

//     char *state_prompt = construct_prompt_for_protocol_states(protocol_name);

//     // Get protocol states
//     get_protocol_message_types(state_prompt, states_set);

//     // traverse the states_set
//     khiter_t k;
//     for (k = kh_begin(states_set); k != kh_end(states_set); ++k)
//     {
//         if (kh_exist(states_set, k))
//         {
//             const char *protocol_state = kh_key(states_set, k);
//             printf("## State_traverse: %s\n", protocol_state);
//         }
//     }

//     // Get seeds to states and save them to the in_dir
//     get_seeds_to_states(in_dir, states_set, protocol_name);

//     // char *prompt = NULL;
//     // asprintf(&prompt, "user: The colors of flowers:\\nassistant: red and yellow.\\nuser: Other colors are:");
//     // printf("## Prompt to LLM:\n %s\n", prompt);
//     // char *answer = chat_with_llm(prompt, "instruct");
//     // printf("## Answer from LLM:\n %s\n", answer);

//     char *protocol_name = argv[1];
//     khash_t(consistency_table) *const_table = kh_init(consistency_table);
//     klist_t(rang) *protocol_patterns = kl_init(rang);

//     for (int iter = 0; iter < 5; iter++)
//     {

//         char *templates_prompt = construct_prompt_for_templates(protocol_name);
//         char *templates_answer = chat_with_llm(templates_prompt, "turbo");
//         // printf("## Answer from LLM:\n %s\n", templates_answer);
//         char *remaining_prompt = construct_prompt_for_remaining_templates(protocol_name, templates_prompt, templates_answer);
//         // printf("remaining prompt is:\n %s\n", remaining_prompt);
//         char *remaining_templates = chat_with_llm(remaining_prompt, "turbo");
//         // printf("## Remaining templates:\n %s\n", remaining_templates);

//         char *combined_templates = NULL;
//         asprintf(&combined_templates, "%s\n%s", templates_answer, remaining_templates);

//         printf("The final info is\n%s\n", combined_templates);
//         klist_t(gram) *grammar_list = kl_init(gram);
//         extract_message_grammars(combined_templates, grammar_list);

//         kliter_t(gram) * iter;
//         for (iter = kl_begin(grammar_list); iter != kl_end(grammar_list); iter = kl_next(iter))
//         {
//             json_object *jobj = kl_val(iter);

//             json_object *header = json_object_array_get_idx(jobj, 0);

//             int absent;

//             const char *header_str = json_object_get_string(header);

//             khiter_t k = kh_put(consistency_table, const_table, header_str, &absent);
//             if (absent)
//             {
//                 khash_t(field_table) *field_table = kh_init(field_table);
//                 kh_value(const_table, k) = field_table;
//             }

//             for (int i = 1; i < json_object_array_length(jobj); i++)
//             {
//                 const char *v = json_object_get_string(json_object_array_get_idx(jobj, i));
//                 khash_t(field_table) *field_table = kh_value(const_table, k);
//                 khiter_t field_k = kh_put(field_table, field_table, v, &absent);
//                 if (absent)
//                 {
//                     kh_value(field_table, field_k) = 0;
//                 }
//                 kh_value(field_table, field_k)++;
//             }
//         }
//         kl_destroy_gram(grammar_list);
//     }

//     for (khiter_t con_t_iter = kh_begin(const_table); con_t_iter != kh_end(const_table); ++con_t_iter)
//     {
//         if (kh_exist(const_table, con_t_iter))
//         {
//             pcre2_code **patterns = ck_alloc(2 * sizeof(pcre2_code *));

//             khash_t(field_table) *field_table = kh_value(const_table, con_t_iter);
//             const char* header_str = json_object_to_json_string(json_object_new_string(kh_key(const_table, con_t_iter)));

//             extract_message_pattern_k(header_str,field_table, patterns);
//             *kl_pushp(rang, protocol_patterns) = patterns;
//         }
//     }

//     char *demo_lines[] = {

//         "DESCRIBE 123\r\n"
//         "CSeq: 1212313\r\n"
//         "User-Agent: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n",

//         "DESCRIBE 123\r\n"
//         "DESCRIBE 123\r\n"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n",

//         "DESCRIBE 123\r\n"
//         "1231321321321"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r\n"
//         "Accept: 1212313\r\n"
//         "\r\n"
//         "1231321321321",

//         "DESCRIBE 123\r\n"
//         "1231321321321"
//         "User-Agent: 1212313\r\n"
//         "CSeq: 1212313\r1231321321321\n"
//         "Accept: 1212313\r\n"
//         "\r\n"
//         "1231321321321",

//         "PLAY 123\r\n"
//         "CSeq: 1212313\r\n"
//         "DESCRIBE 123\r\n"
//         "User-Agent: 1212313\r\n"
//         "Session: 1212313\r\n"
//         "Range: 1212313\r\n"
//         "\r\n",

//     };

// char* answers = "For the RTSP protocol, the DESCRIBE client request template is:"
//     "{\"DESCRIBE\":\"string\\r\\n\",\"CSeq:\":\"integer\\r\\n\",\"User-Agent:\":\"string\\r\\n\",\"Accept:\":\"string\\r\\n\\r\\n\"}."
//     "For the RTSP protocol, the DESCRIBE client request template is:{\"DESCRIBE\":\"string\\r\\n\",\"CSeq:\":\"integer\\r\\n\",\"User-Agent:\":\"string\\r\\n\",\"Accept:\":\"string\\r\\n\\r\\n\"}";

// for (int demo = 0; demo < sizeof(demo_lines) / sizeof(char *); demo++)
// {
//     printf("\nTrying to match \n%s\n\n", demo_lines[demo]);
//     int max_rc = -1;
//     kliter_t(rang) * iter_rang;
//     range_list max_ranges;
//     int i = 0;
//     for (iter_rang = kl_begin(protocol_patterns); iter_rang != kl_end(protocol_patterns); iter_rang = kl_next(iter_rang),i++)
//     {
//         // printf("Compare! \n");

//         pcre2_code **patterns = kl_val(iter_rang);
//         pcre2_code *header_pattern = patterns[0];
//         pcre2_code *fields_pattern = patterns[1];

//         range_list header_ranges = starts_with(demo_lines[demo], strlen(demo_lines[demo]), header_pattern);
//         kv_init(header_ranges);

//         if (kv_size(header_ranges) == 0)
//         {
//             printf("Demo %d Did not match pattern %d\n", demo, i);
//             continue;
//         }
//         else
//         {
//             printf("Demo %d Did matched pattern %d\n", demo, i);
//             range header_match = kv_pop(header_ranges);
//             char *offsetted_line = demo_lines[demo];
//             size_t offsetted_len = strlen(demo_lines[demo]);
//             range_list field_ranges = get_mutable_ranges(offsetted_line,offsetted_len, header_match.len,fields_pattern);

//             for(int i = 0; i < kv_size(field_ranges);i++){
//                 kv_push(range, header_ranges, kv_A(field_ranges,i));
//             }
//             kv_destroy(field_ranges);

//             max_ranges = header_ranges;

//             break;
//         }
//     }

//     if (max_rc != -1)
//     {
//         printf("Matched! \n");
//         for (int i = 0; i < max_rc; i++)
//         {
//             printf("start=%d len=%d mutable=%d\n", kv_A(max_ranges,i).start,kv_A(max_ranges,i).len, kv_A(max_ranges,i).mutable);
//             printf("content=%s\n", json_object_to_json_string(json_object_new_string_len(demo_lines[demo] + kv_A(max_ranges,i).start, kv_A(max_ranges,i).len)));
//         }
//     }
//     else
//     {
//         printf("No matches\n");
//     }
// }

// Traverse the list

//     return 0;
// }
