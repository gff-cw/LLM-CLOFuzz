#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chat-llm.h"
#include "alloc-inl.h"
int main(int argc,char **argv) {
  if(argc!=2) return 3;
  if(!strcmp(argv[1],"json")) {
    char answer[]="[\"GET <<PATH>> HTTP/1.1\\r\\n\",\"Host: [::1]\\r\\n\"]";
    klist_t(gram)*list=kl_init(gram);
    extract_message_grammars(answer,list);
    unsigned valid=0;
    for(kliter_t(gram)*it=kl_begin(list);it!=kl_end(list);it=kl_next(it)) {
      json_object *value=kl_val(it);
      if(value && json_object_is_type(value,json_type_array) && json_object_array_length(value)==2) valid++;
      if(value) json_object_put(value);
    }
    kl_destroy_gram(list);
    printf("valid_ipv6_grammars=%u\n",valid);
    return valid==1?0:2;
  }
  if(!strcmp(argv[1],"header")) {
    char raw[124]; memset(raw,'A',123); raw[123]=0;
    json_object *header=json_object_new_string(raw);
    khash_t(field_table)*fields=kh_init(field_table);
    pcre2_code *patterns[2]={0};
    char *type=extract_message_pattern(json_object_to_json_string(header),fields,patterns,-1,NULL);
    int ok=type && patterns[0] && patterns[1];
    if(type) ck_free(type);
    for(int i=0;i<2;i++) if(patterns[i]) pcre2_code_free(patterns[i]);
    kh_destroy(field_table,fields); json_object_put(header);
    printf("long_header_accepted=%d\n",ok);
    return ok?0:2;
  }
  return 3;
}
