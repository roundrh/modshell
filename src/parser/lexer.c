#include"lexer.h"

int init_token_stream(t_token_stream* token_stream){

    token_stream->tokens = malloc(INITIAL_TOKS_ARR_CAP * sizeof(t_token));
    if(token_stream->tokens == NULL){
        perror("fatal malloc init tokens arr");
        return -1;
    }
    token_stream->tokens_arr_cap = INITIAL_TOKS_ARR_CAP;
    for(size_t i = 0; i < INITIAL_TOKS_ARR_CAP; i++) {
        token_stream->tokens[i].type = -1;
        token_stream->tokens[i].start = NULL;
        token_stream->tokens[i].len = 0;
    }

    token_stream->tokens_arr_len = 0;
    return 0;
}

t_token_type get_token_type(const char *c, size_t *len){

    if (!c || !len)
        return -1;

    if (c[0] == '|' && c[1] == '|') { *len = 2; return TOKEN_OR; }
    if (c[0] == '&' && c[1] == '&') { *len = 2; return TOKEN_AND; }
    if (c[0] == '>' && c[1] == '>') { *len = 2; return TOKEN_APPEND; }
    if (c[0] == '<' && c[1] == '<') { *len = 2; return TOKEN_HEREDOC; }

    if (c[0] == '=') { *len = 1; return TOKEN_EQUAL; }
    if (c[0] == '|') { *len = 1; return TOKEN_PIPE; }
    if (c[0] == '&') { *len = 1; return TOKEN_BG; }
    if (c[0] == '>') { *len = 1; return TOKEN_TRUNC; }
    if (c[0] == '<') { *len = 1; return TOKEN_INPUT; }
    if (c[0] == '(') { *len = 1; return TOKEN_OPEN_PAR; }
    if (c[0] == ')') { *len = 1; return TOKEN_CLOSE_PAR; }
    if (c[0] == ';') { *len = 1; return TOKEN_SEQ; }
    if (c[0] == '{') { *len = 1; return TOKEN_OPEN_BRACE; }
    if (c[0] == '}') { *len = 1; return TOKEN_CLOSE_BRACE; }

    return TOKEN_SIMPLE;
}

static int check_realloc_toks_arr(t_token_stream* ts, size_t tok_count){

    if(tok_count + 2 < ts->tokens_arr_cap)
        return 0;

    size_t new_cap = (ts->tokens_arr_cap) * BUF_GROWTH_FACTOR;
    t_token* new_toks_arr = realloc(ts->tokens, new_cap * sizeof(t_token));
    if(!new_toks_arr){
        perror("fatal realloc");
        return -1;
    }

    ts->tokens = new_toks_arr;
    for (size_t i = ts->tokens_arr_cap; i < new_cap; i++){
        ts->tokens[i].len = 0;
        ts->tokens[i].type = -1;
        ts->tokens[i].start = NULL;
    }

    ts->tokens_arr_cap = new_cap;

    return 0;
}

static void flush_word(t_token_stream* ts, char** tok_start, size_t* tok_len, bool* tokenized, size_t* tok_count){

    if(*tokenized == false)
        return;

    ts->tokens[*tok_count].start = *tok_start;
    ts->tokens[*tok_count].len   = *tok_len;
    ts->tokens[*tok_count].type  = TOKEN_SIMPLE;
    (*tok_count)++;

    *tok_start = NULL;
    *tok_len = 0;
    *tokenized = false;
}

/*buffer safe because userinp.c null-terminates buffer. paired with while loop cond cmd_buf[i+1] can be '\0' but never UB*/
int lex_command_line(char* cmd_buf, t_token_stream* token_stream){

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool tokenized = false;
    char* tok_start = NULL;
    size_t word_len = 0;
    size_t op_len = 0;
    int i = 0;
    size_t token_count = 0;
    while(cmd_buf[i] != '\0'){

        if(check_realloc_toks_arr(token_stream, token_count) == -1){
            return -1;
        }

        if (!in_single_quote && !in_double_quote && cmd_buf[i] == '\''){
            in_single_quote = true;
            if (!tokenized){
                tok_start = &cmd_buf[i];
                tokenized = true; 
            }
        } else if (!in_single_quote && !in_double_quote && cmd_buf[i] == '"'){
            in_double_quote = true;
            if (!tokenized){
                tok_start = &cmd_buf[i];
                tokenized = true; 
            }
        }else if(!in_single_quote && !in_double_quote){

            char seq[3] = {cmd_buf[i], cmd_buf[i + 1], '\0'};
            t_token_type type = get_token_type(seq, &op_len);

            if(cmd_buf[i] == ' '){
                flush_word(token_stream, &tok_start, &word_len, &tokenized, &token_count);
                i++;
                continue;
            } else if(type != TOKEN_SIMPLE){

                flush_word(token_stream, &tok_start, &word_len, &tokenized, &token_count);
                token_stream->tokens[token_count].start = &cmd_buf[i];
                token_stream->tokens[token_count].len = op_len;
                token_stream->tokens[token_count].type = type;  
                token_count++;
                tokenized = false;

                i+=op_len;
                continue;
            } else if(!tokenized){
                tok_start = &cmd_buf[i];
                tokenized = true;
                word_len++;
                i++;
                continue;
            }
        } else{
            
            if(in_single_quote && cmd_buf[i] == '\''){
                in_single_quote = false;
            }
            if(in_double_quote && cmd_buf[i] == '"'){
                in_double_quote = false;
            }
        }

        if(tokenized){
            word_len++;
        }
        i++;
    }

    flush_word(token_stream, &tok_start, &word_len, &tokenized, &token_count);

    /* state should never still be in quotes */
    if(in_single_quote || in_double_quote){
        fprintf(stderr, "\nmsh: syntax err unbalanced quotes");
        return -1;
    }

    token_stream->tokens_arr_len = token_count;
    return 0;
}

int cleanup_token_stream(t_token_stream* token_stream){

    if(token_stream->tokens)
        free(token_stream->tokens);

    token_stream->tokens = NULL;
    token_stream->tokens_arr_cap = -1;
    token_stream->tokens_arr_len = -1;
    
    return 0;
}