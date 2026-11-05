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
    const char *candidate_paths[4] = {NULL};
    int count = 0;

    if (path && *path)
        candidate_paths[count++] = path;

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
        g_llm_config.loaded = 1;
        printf("[-] Warning: 'llm_config.json' not found in working directory; using default configuration (Model: %s)\n", g_llm_config.model);
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

    const char *base_url = (cfg->base_url[0]) ? cfg->base_url : "https://api.openai.com/v1";
    int is_custom_base = (cfg->base_url[0] && strcmp(cfg->base_url, "https://api.openai.com/v1") != 0);

    char clean_base[256];
    strncpy(clean_base, base_url, sizeof(clean_base) - 1);
    clean_base[sizeof(clean_base) - 1] = '\0';
    size_t blen = strlen(clean_base);
    while (blen > 0 && clean_base[blen - 1] == '/')
        clean_base[--blen] = '\0';

    if (is_legacy_instruct)
        snprintf(default_url, sizeof(default_url), "%s/completions", clean_base);
    else
        snprintf(default_url, sizeof(default_url), "%s/chat/completions", clean_base);

    const char *token = (cfg->api_key[0] && strcmp(cfg->api_key, "YOUR_API_KEY_HERE") != 0) ? cfg->api_key : NULL;

    char auth_header[512] = {0};
    if (token && *token)
    {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    }
    else if (!is_custom_base)
    {
        static int warned_token = 0;
        if (!warned_token)
        {
            printf("[-] Warning: No valid API Key configured in 'llm_config.json'. Skipping LLM request.\n");
            warned_token = 1;
        }
        return NULL;
    }

    int max_tokens = cfg->max_tokens > 0 ? cfg->max_tokens : MAX_TOKENS;

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
    long connect_timeout_sec = 15L;

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

/* json-c takes ownership only when the add succeeds. A required string must
   not silently become JSON null when its allocation fails. */
static int stall_add_string(json_object *object, const char *key, const char *text)
{
    json_object *value = json_object_new_string(text);
    if (!value)
        return -1;
    if (json_object_object_add(object, key, value) < 0)
    {
        json_object_put(value);
        return -1;
    }
    return 0;
}

char *construct_prompt_stall(char *protocol_name, char *examples, char *history)
{
    char *template = "In the %s protocol, the communication history between the %s client and the %s server is as follows."
                     "The next proper client request that can affect the server's state are:\n\n"
                     "Desired format of real client requests:\n%sCommunication History:\n\"\"\"\n%s\"\"\"";

    char *prompt = NULL;
    if (asprintf(&prompt, template, protocol_name ? protocol_name : "",
                 protocol_name ? protocol_name : "",
                 protocol_name ? protocol_name : "",
                 examples ? examples : "",
                 history ? history : "") < 0 || !prompt) {
        return NULL;
    }

    char *final_prompt = NULL;
    json_object *messages_obj = NULL;
    json_object *sys_msg = NULL;
    json_object *user_msg = NULL;

    messages_obj = json_object_new_array();
    if (!messages_obj)
        goto cleanup;

    sys_msg = json_object_new_object();
    if (!sys_msg ||
        stall_add_string(sys_msg, "role", "system") < 0 ||
        stall_add_string(sys_msg, "content", "You are a helpful assistant.") < 0)
        goto cleanup;
    if (json_object_array_add(messages_obj, sys_msg) < 0)
        goto cleanup;
    sys_msg = NULL; /* Owned by messages_obj. */

    user_msg = json_object_new_object();
    if (!user_msg ||
        stall_add_string(user_msg, "role", "user") < 0 ||
        stall_add_string(user_msg, "content", prompt) < 0)
        goto cleanup;
    if (json_object_array_add(messages_obj, user_msg) < 0)
        goto cleanup;
    user_msg = NULL; /* Owned by messages_obj. */

    const char *json_payload = json_object_to_json_string(messages_obj);
    if (json_payload)
        final_prompt = strdup(json_payload);

cleanup:
    if (user_msg) json_object_put(user_msg);
    if (sys_msg) json_object_put(sys_msg);
    if (messages_obj) json_object_put(messages_obj);
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
    if (!message) return NULL;

    int message_len = strlen(message);
    int max_len = (message_len > 0 ? message_len : 1);
    int res_len = 0;
    char *res = ck_alloc(max_len * sizeof(char));
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

/* Limits apply to individual grammar data, rather than relying on model output
   length or fixed stack arrays. Pattern length excludes its terminating NUL. */
#define GRAMMAR_RESPONSE_LIMIT (1024U * 1024U)
#define GRAMMAR_JSON_LIMIT (256U * 1024U)
#define GRAMMAR_PATTERN_LIMIT (64U * 1024U)

static json_object *parse_grammar_json(const char *text, size_t length)
{
    if (!text || !length || length > GRAMMAR_JSON_LIMIT)
    {
        errno = length > GRAMMAR_JSON_LIMIT ? E2BIG : EINVAL;
        return NULL;
    }
    /* json-c STRICT still accepts raw control bytes in strings on some
       releases. JSON requires them to be escaped, including raw newlines. */
    int quoted = 0, escaped = 0;
    for (size_t i = 0; i < length; i++)
    {
        unsigned char byte = (unsigned char)text[i];
        if (quoted)
        {
            if (byte < 32) { errno = EINVAL; return NULL; }
            if (escaped) escaped = 0;
            else if (byte == '\\') escaped = 1;
            else if (byte == '"') quoted = 0;
        }
        else if (byte == '"') quoted = 1;
    }
    char *copy = malloc(length + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';

    json_tokener *tokener = json_tokener_new_ex(64);
    if (!tokener)
    {
        free(copy);
        errno = ENOMEM;
        return NULL;
    }
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
    json_object *value = json_tokener_parse_ex(tokener, copy, (int)length + 1);
    enum json_tokener_error error = json_tokener_get_error(tokener);
    size_t consumed = json_tokener_get_parse_end(tokener);
    while (consumed < length && isspace((unsigned char)copy[consumed]))
        consumed++;
    if (error != json_tokener_success || !value || consumed < length)
    {
        if (value) json_object_put(value);
        value = NULL;
        errno = EINVAL;
    }
    json_tokener_free(tokener);
    free(copy);
    return value;
}

static int is_message_grammar(json_object *value)
{
    if (!value || !json_object_is_type(value, json_type_array) ||
        json_object_array_length(value) == 0)
        return 0;
    for (size_t i = 0; i < json_object_array_length(value); i++)
    {
        json_object *part = json_object_array_get_idx(value, i);
        if (!part || !json_object_is_type(part, json_type_string))
            return 0;
        const char *text = json_object_get_string(part);
        size_t length = (size_t)json_object_get_string_len(part);
        /* Downstream hash keys are C strings, so embedded NUL is not lossless. */
        if (!text || strlen(text) != length || (i == 0 && length == 0))
            return 0;
    }
    return 1;
}

void extract_message_grammars(char *answers, klist_t(gram) *grammar_list)
{
    if (!answers || !grammar_list)
        return;
    size_t length = strnlen(answers, GRAMMAR_RESPONSE_LIMIT + 1);
    if (length > GRAMMAR_RESPONSE_LIMIT)
    {
        errno = E2BIG;
        return;
    }

    const char *cursor = answers;
    const char *limit = answers + length;
    while (cursor < limit)
    {
        const char *start = memchr(cursor, '[', (size_t)(limit - cursor));
        if (!start) break;
        const char *end = NULL;
        size_t depth = 0;
        int quoted = 0, escaped = 0;
        for (const char *at = start; at < limit; at++)
        {
            if (quoted)
            {
                if (escaped) escaped = 0;
                else if (*at == '\\') escaped = 1;
                else if (*at == '"') quoted = 0;
            }
            else if (*at == '"') quoted = 1;
            else if (*at == '[') depth++;
            else if (*at == ']' && --depth == 0)
            {
                end = at + 1;
                break;
            }
        }
        /* An unfinished candidate has no trustworthy boundary to resynchronize. */
        if (!end) break;
        cursor = end;
        json_object *value = parse_grammar_json(start, (size_t)(end - start));
        if (!is_message_grammar(value))
        {
            if (value) json_object_put(value);
            continue;
        }
        /* The caller owns accepted JSON objects. Do not release them here:
           setup_llm_grammars borrows their strings as consistency-table keys. */
        *kl_pushp(gram, grammar_list) = value;
    }
}

struct grammar_pattern_buffer
{
    char *data;
    size_t length;
    size_t capacity;
};

static int append_grammar_pattern(struct grammar_pattern_buffer *buffer,
                                  const char *text, size_t length)
{
    if (length > GRAMMAR_PATTERN_LIMIT - buffer->length)
    {
        errno = E2BIG;
        return 0;
    }
    size_t needed = buffer->length + length + 1;
    if (needed > buffer->capacity)
    {
        size_t capacity = buffer->capacity ? buffer->capacity : 128;
        while (capacity < needed)
            capacity = capacity > (GRAMMAR_PATTERN_LIMIT + 1) / 2 ?
                       GRAMMAR_PATTERN_LIMIT + 1 : capacity * 2;
        char *grown = realloc(buffer->data, capacity);
        if (!grown)
            return 0;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (length) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int append_grammar_literal(struct grammar_pattern_buffer *buffer,
                                  const char *text, size_t length)
{
    static const char metacharacters[] = "\\.^$|()[]{}*+?";
    for (size_t i = 0; i < length; i++)
    {
        unsigned char byte = (unsigned char)text[i];
        if (byte < 32 || byte == 127)
        {
            static const char hex[] = "0123456789abcdef";
            char escape[] = {'\\', 'x', hex[byte >> 4], hex[byte & 15]};
            if (!append_grammar_pattern(buffer, escape, sizeof(escape))) return 0;
        }
        else
        {
            if (strchr(metacharacters, byte) &&
                !append_grammar_pattern(buffer, "\\", 1)) return 0;
            if (!append_grammar_pattern(buffer, text + i, 1)) return 0;
        }
    }
    return 1;
}

static int append_grammar_template(struct grammar_pattern_buffer *buffer,
                                   const char *text)
{
    if (!text) { errno = EINVAL; return 0; }
    size_t length = strnlen(text, GRAMMAR_JSON_LIMIT + 1);
    if (length > GRAMMAR_JSON_LIMIT)
    {
        errno = E2BIG;
        return 0;
    }
    if (!append_grammar_pattern(buffer, "(?:", 3)) return 0;
    const char *cursor = text;
    while (*cursor)
    {
        const char *open = strstr(cursor, "<<");
        const char *close = open ? strstr(open + 2, ">>") : NULL;
        if (!open || !close || close == open + 2)
        {
            if (!append_grammar_literal(buffer, cursor, strlen(cursor))) return 0;
            break;
        }
        if (!append_grammar_literal(buffer, cursor, (size_t)(open - cursor)) ||
            !append_grammar_pattern(buffer, "(.*)", 4)) return 0;
        cursor = close + 2;
    }
    return append_grammar_pattern(buffer, ")", 1);
}

/* header_str is a serialized JSON string; field_table keys are already-decoded
   strings. Only <<...>> placeholders create capture groups. Literal fragments
   are escaped once for PCRE, keeping quotes, brackets, backslashes and CR/LF. */
char *extract_message_pattern(const char *header_str, khash_t(field_table) *field_table,
                              pcre2_code **patterns, int debug_file,
                              const char *debug_file_name)
{
    if (!patterns) { errno = EINVAL; return NULL; }
    patterns[0] = patterns[1] = NULL;
    if (!header_str || !field_table) { errno = EINVAL; return NULL; }

    size_t encoded_length = strnlen(header_str, GRAMMAR_JSON_LIMIT + 1);
    json_object *header = parse_grammar_json(header_str, encoded_length);
    struct grammar_pattern_buffer header_pattern = {0}, fields_pattern = {0};
    pcre2_code *header_code = NULL, *fields_code = NULL;
    char *message_type = NULL;
    if (!header) goto fail;
    if (!json_object_is_type(header, json_type_string)) { errno = EINVAL; goto fail; }
    const char *text = json_object_get_string(header);
    size_t text_length = (size_t)json_object_get_string_len(header);
    if (!text || !text_length || strlen(text) != text_length) { errno = EINVAL; goto fail; }
    size_t message_length = strcspn(text, " \t\r\n\\");
    if (!message_length) { errno = EINVAL; goto fail; }

    if (!append_grammar_pattern(&header_pattern, "^", 1) ||
        !append_grammar_template(&header_pattern, text) ||
        !append_grammar_pattern(&fields_pattern, "(?|", 3)) goto fail;
    int first = 1;
    for (khiter_t it = kh_begin(field_table); it != kh_end(field_table); it++)
    {
        if (!kh_exist(field_table, it) ||
            kh_value(field_table, it) < (TEMPLATE_CONSISTENCY_COUNT + 1) / 2)
            continue;
        if (!first && !append_grammar_pattern(&fields_pattern, "|", 1)) goto fail;
        if (!append_grammar_template(&fields_pattern, kh_key(field_table, it))) goto fail;
        first = 0;
    }
    /* No agreed fields: retain the original (.+) fallback. */
    if (first)
    {
        fields_pattern.length = 0;
        if (!append_grammar_pattern(&fields_pattern, "(.+)", 4)) goto fail;
    }
    else if (!append_grammar_pattern(&fields_pattern, ")", 1)) goto fail;

    int errornumber;
    PCRE2_SIZE erroroffset;
    header_code = pcre2_compile((PCRE2_SPTR)header_pattern.data, header_pattern.length,
                               0, &errornumber, &erroroffset, NULL);
    if (!header_code) { errno = EINVAL; goto fail; }
    /* JIT is optional. Failed JIT leaves valid interpreted matching available. */
    (void)pcre2_jit_compile(header_code, PCRE2_JIT_COMPLETE);
    fields_code = pcre2_compile((PCRE2_SPTR)fields_pattern.data, fields_pattern.length,
                               0, &errornumber, &erroroffset, NULL);
    if (!fields_code) { errno = EINVAL; goto fail; }
    (void)pcre2_jit_compile(fields_code, PCRE2_JIT_COMPLETE);

    message_type = ck_alloc(message_length + 1);
    memcpy(message_type, text, message_length);
    message_type[message_length] = '\0';
    if (debug_file >= 0 && debug_file_name)
    {
        ck_write(debug_file, header_pattern.data, header_pattern.length, debug_file_name);
        ck_write(debug_file, "\n", 1, debug_file_name);
        ck_write(debug_file, fields_pattern.data, fields_pattern.length, debug_file_name);
    }
    patterns[0] = header_code;
    patterns[1] = fields_code;
    json_object_put(header);
    free(header_pattern.data);
    free(fields_pattern.data);
    return message_type;

fail:
    if (header_code) pcre2_code_free(header_code);
    if (fields_code) pcre2_code_free(fields_code);
    if (header) json_object_put(header);
    free(header_pattern.data);
    free(fields_pattern.data);
    return NULL;
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
    if (enriched_file == NULL || contents == NULL) return;

    FILE *fp = fopen(enriched_file, "wb");
    if (fp == NULL)
    {
        printf("Error in opening the file %s\n", enriched_file);
        return;
    }

    // remove the newline and whitespace in the beginning of the string if any
    while (contents[0] == '\n' || contents[0] == ' ' || contents[0] == '\t' || contents[0] == '\r')
    {
        contents++;
    }

    int len = strlen(contents);
    if (len == 0)
    {
        fclose(fp);
        return;
    }

    /* Write pure raw stream without artificial \r\n\r\n replay framing.
       Boundary identification is handled by protocol-specific extract_requests. */
    fwrite(contents, 1, len, fp);

    fclose(fp);
}

char *format_string(char *state_string)
{
    if (!state_string) return NULL;

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
    const llm_config_t *cfg = get_llm_config();
    int ctx_limit = (cfg && cfg->max_tokens > 0) ? cfg->max_tokens : MAX_TOKENS;

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
