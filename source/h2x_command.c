#include <h2x_command.h>

#include <h2x_buffer.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int command_split(struct h2x_buffer* buffer, char** command, char** command_args)
{
    *command = NULL;
    *command_args = NULL;

    char* endline = memchr(buffer->data, '\n', buffer->write_position);
    if(endline == NULL)
    {
        return -1;
    }

    int command_line_index = endline - buffer->data;
    buffer->data[command_line_index] = 0;

    int command_begin_index = 0;
    while(command_begin_index < command_line_index)
    {
        if(!isspace(buffer->data[command_begin_index]))
        {
            break;
        }
        ++command_begin_index;
    }

    int command_end_index = command_begin_index;
    while(command_end_index < command_line_index)
    {
        if(isspace(buffer->data[command_end_index]))
        {
            break;
        }

        ++command_end_index;
    }

    buffer->data[command_end_index] = 0;
    *command = buffer->data + command_begin_index;

    if(command_end_index < command_line_index)
    {
        *command_args = buffer->data + command_end_index + 1;
    }
    else
    {
        *command_args = buffer->data + command_line_index;
    }

    return command_line_index;
}

static void command_pullup(struct h2x_buffer* buffer, int command_length)
{
  if(command_length < 0)
  {
    return;
  }

  int pullup_length = command_length + 1;
  int copy_length = buffer->write_position - pullup_length;
  if(copy_length > 0)
  {
    memmove(buffer->data, buffer->data + pullup_length, copy_length);
  }

  buffer->write_position = copy_length;
}

static void split_args(struct command_def* command, char* command_args, int* argc, char*** argv)
{
    int arg_index = 0;
    int arg_end = strlen(command_args);
    int arg_count = 0;

    *argc = 0;
    *argv = NULL;
    bool in_arg = false;
    bool in_quote = false;

    while(arg_index < arg_end)
    {
        if(arg_count + 1 >= command->required_arguments && command->capture_all_last_required)
        {
            ++arg_count;
            break;
        }

        char current_char = command_args[arg_index];
        bool is_space = isspace(current_char);
        if(!in_arg)
        {
            ++arg_index;
            if(!is_space)
            {
                in_arg = true;
                in_quote = current_char == '"';
                ++arg_count;
            }

            continue;
        }

        if((is_space && !in_quote) || (in_quote && current_char == '"'))
        {
            in_arg = false;
            in_quote = false;
            command_args[arg_index] = 0;
        }

        ++arg_index;
    }

    if(arg_count == 0)
    {
        return;
    }

    *argc = arg_count;
    *argv = calloc(arg_count, sizeof(char *));

    in_arg = false;
    in_quote = false;
    arg_index = 0;
    int current_arg = 0;

    while(arg_index < arg_end && current_arg < arg_count)
    {
        if(arg_count + 1 >= command->required_arguments && command->capture_all_last_required)
        {
            (*argv)[current_arg] = command_args + arg_index;
            break;
        }

        char current_char = command_args[arg_index];
        bool is_space = isspace(current_char);
        if(!in_arg)
        {
            if(!is_space)
            {
                in_arg = true;
                in_quote = current_char == '"';
                (*argv)[current_arg] = command_args + arg_index + (in_quote ? 1 : 0);

                ++current_arg;
            }

            ++arg_index;
            continue;
        }

        if(current_char == 0)
        {
            in_arg = false;
            in_quote = false;
        }

        ++arg_index;
    }
}

static void process_command_line(struct command_def* command_definitions, int command_count, char *command, char *command_args)
{
    for(uint32_t i = 0; i < command_count; ++i)
    {
        if(strcmp(command_definitions[i].command, command) == 0)
        {
            int argc = 0;
            char** argv = NULL;
            split_args(&command_definitions[i], command_args, &argc, &argv);

            if(argc >= command_definitions[i].required_arguments)
            {
                command_definitions[i].handler(argc, argv);
            }
            else
            {
                fprintf(stderr, "Insufficient arguments for command %s\n", command);
                fprintf(stderr, "Help:\n %s - %s\n", command, command_definitions[i].help);
            }

            free(argv);
            return;
        }
    }

    fprintf(stderr, "Unknown command: %s\n", command);
}

void h2x_command_process(struct h2x_buffer* buffer, struct command_def* command_definitions, int command_count)
{
    while(1)
    {
        char* command_begin = NULL;
        char* command_args_begin = NULL;
        int command_length = command_split(buffer, &command_begin, &command_args_begin);
        if(command_length < 0)
        {
            break;
        }


        process_command_line(command_definitions, command_count, command_begin, command_args_begin);
        command_pullup(buffer, command_length);
    }
}