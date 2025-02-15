#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <leif/leif.h>
#include <string.h>

typedef enum
{
    TAB_DASHBOARD = 0,
    TAB_NEW_TASK
} gui_tab;
typedef enum
{
    FILTER_ALL = 0,
    FILTER_IN_PROGRESS,
    FILTER_COMPLETED,
    FILTER_LOW,
    FILTER_MEDIUM,
    FILTER_HIGH
} entry_filter;

typedef enum
{
    PRIORITY_LOW = 0,
    PRIORITY_MEDIUM,
    PRIORITY_HIGH
} entry_priority;

typedef struct
{
    bool completed;
    char *desc, *date;
    entry_priority priority;
} task_entry;
#define WIN_MARGIN 20.0f

static int winw = 1280, winh = 720;
static LfFont titlefont, smallfont;
static entry_filter current_filter;
static gui_tab current_tab;

static task_entry *entries[1024];
static uint32_t numEntries = 0;

static LfTexture removeTexture, backTexture;

static LfInputField newTaskInput;
static char newTaskInputBuf[512];

static void serialiseTodoEntry(FILE *file, task_entry *entry)
{
    fwrite(&entry->completed, sizeof(bool), 1, file);

    size_t desclen = strlen(entry->desc + 1); // +1 for the null terminator
    fwrite(&desclen, sizeof(size_t), 1, file);
    fwrite(entry->desc, sizeof(char), desclen, file);

    size_t datelen = strlen(entry->date + 1); // +1 for the null terminator
    fwrite(&datelen, sizeof(size_t), 1, file);
    fwrite(entry->date, sizeof(char), datelen, file);

    fwrite(&entry->priority, sizeof(entry_priority), 1, file);
}
static void serialiseTodoList(const char *filename)
{
    FILE *file = fopen(filename, "wb");
    if (!file)
    {
        printf("failed to open data file.\n");
        return;
    }
    for (uint32_t i = 0; i < numEntries; i++)
    {
        serialiseTodoEntry(file, entries[i]);
    }
}

task_entry *deserialiseTodoEntry(FILE *file)
{
    task_entry *entry = (task_entry *)malloc(sizeof(*entry));

    if (fread(&entry->completed, sizeof(bool), 1, file) != 1)
    {
        free(entry);
        return NULL;
    }

    size_t desclen;
    if (fread(&desclen, sizeof(size_t), 1, file) != 1)
    {
        free(entry);
        return NULL;
    }
    entry->desc = malloc(desclen);
    if (!entry->desc)
    {
        free(entry);
        return NULL;
    }
    if (fread(entry->desc, sizeof(char), desclen, file) != desclen)
    {
        free(entry->desc);
        free(entry);
        return NULL;
    }

    size_t datelen;
    if (fread(&datelen, sizeof(size_t), 1, file) != 1)
    {
        free(entry->desc);
        free(entry);
        return NULL;
    }
    entry->date = malloc(datelen);
    if (!entry->date)
    {
        free(entry->desc);
        free(entry);
        return NULL;
    }
    if (fread(entry->date, sizeof(char), datelen, file) != datelen)
    {
        free(entry->desc);
        free(entry->date);
        free(entry);
        return NULL;
    }

    if (fread(&entry->priority, sizeof(entry_priority), 1, file) != 1)
    {
        free(entry->desc);
        free(entry->date);
        free(entry);
        return NULL;
    }
    return entry;
}

void deserialise_todo_list(const char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        file = fopen(filename, "w");
        fclose(file);
        file = fopen(filename, "rb");
    }
    task_entry *entry;
    while ((entry = deserialiseTodoEntry(file)) != NULL)
    {
        entries[numEntries++] = entry;
    }
    fclose(file);
}
char *get_command_output(const char *cmd)
{
    FILE *fp;
    char buffer[1024];
    char *result = NULL;
    size_t result_size = 0;

    // opening a new pipe with the given command
    fp = popen(cmd, "r");
    if (fp == NULL)
    {
        printf("Failed to run command\n");
        return NULL;
    }

    // reading the output
    while (fgets(buffer, sizeof(buffer), fp) != NULL)
    {
        size_t buffer_len = strlen(buffer);
        char *temp = realloc(result, result_size + buffer_len + 1);
        if (temp == NULL)
        {
            printf("Memory allocation failed\n");
            free(result);
            pclose(fp);
            return NULL;
        }
        result = temp;
        strcpy(result + result_size, buffer);
        result_size += buffer_len;
    }
    pclose(fp);
    return result;
}

static int compareEntryPriority(const void *a, const void *b)
{
    task_entry *entry_a = *(task_entry **)a;
    task_entry *entry_b = *(task_entry **)b;
    return (entry_b->priority - entry_a->priority);
}
static void sortEntries()
{
    qsort(entries, numEntries, sizeof(task_entry *), compareEntryPriority);
}
static void rendertopbar()
{
    lf_push_font(&titlefont);
    lf_text("your to-dos");
    lf_pop_font();

    {
        const float width = 160.0f;

        lf_set_ptr_x_absolute(winw - width - WIN_MARGIN * 3.0f);
        LfUIElementProps props = lf_get_theme().button_props;
        props.margin_left = 0.0f;
        props.margin_right = 0.0f;
        props.color = (LfColor){65, 167, 204, 255};
        props.border_width = 0.0f;
        props.corner_radius = 4.0f;
        lf_push_style_props(props);
        lf_set_line_should_overflow(false);
        //-1 just takes the normal height?
        if (lf_button_fixed("New task", width, -1) == LF_CLICKED)
        {
            current_tab = TAB_NEW_TASK;
        }
        lf_set_line_should_overflow(true);
        lf_pop_style_props();
    }
}

static void renderFilters()
{
    const uint32_t numfilters = 6;
    static const char *filters[] =
        {"all", "in progress", "completed", "low", "medium", "high"};

    LfUIElementProps props = lf_get_theme().button_props;
    props.margin_left = 10.0f;
    props.margin_right = 10.0f;
    props.margin_top = 30.0f;
    props.padding = 10.0f;
    props.border_width = 0.0f;
    props.color = LF_NO_COLOR;
    props.text_color = LF_WHITE;
    props.corner_radius = 5.0f;

    float width = 0.0f;
    float ptrx_before = lf_get_ptr_x();
    float ptry_before = lf_get_ptr_y();
    lf_push_style_props(props);
    lf_set_no_render(true);
    lf_set_ptr_y_absolute(lf_get_ptr_y() + 50.0f);
    for (uint32_t i = 0; i < numfilters; i++)
    {
        lf_button(filters[i]);
    }
    lf_set_no_render(false);
    lf_set_ptr_y_absolute(ptry_before);

    width = lf_get_ptr_x() - ptrx_before - props.margin_right - props.padding;

    lf_set_ptr_x_absolute(winw - width - WIN_MARGIN * 2.0f);

    lf_set_line_should_overflow(false);

    for (uint32_t i = 0; i < numfilters; i++)
    {
        props.color = (current_filter == (entry_filter)i) ? (LfColor){255, 255, 255, 50} : LF_NO_COLOR;
        lf_push_style_props(props);
        if (lf_button(filters[i]) == LF_CLICKED)
        {
            current_filter = (entry_filter)i;
        }
        lf_pop_style_props();
    }
    lf_set_line_should_overflow(true);
}

static void renderEntries()
{
    lf_div_begin(
        ((vec2s){lf_get_ptr_x(), lf_get_ptr_y()}),
        ((vec2s){winw - lf_get_ptr_x() - WIN_MARGIN,
                 (winh - lf_get_ptr_y() - WIN_MARGIN)}),
        true);

    uint32_t renderedCount = 0;
    for (uint32_t i = 0; i < numEntries; i++)
    {
        task_entry *entry = entries[i];
        if (current_filter == FILTER_LOW && entry->priority != PRIORITY_LOW)
            continue;
        if (current_filter == FILTER_MEDIUM && entry->priority != PRIORITY_MEDIUM)
            continue;
        if (current_filter == FILTER_HIGH && entry->priority != PRIORITY_HIGH)
            continue;
        if (current_filter == FILTER_COMPLETED && !entry->completed)
            continue;
        if (current_filter == FILTER_IN_PROGRESS && entry->completed)
            continue;
        float priority_size = 15.0f;
        float ptry_before = lf_get_ptr_y();
        lf_set_ptr_y_absolute(lf_get_ptr_y() + 5.0f);
        lf_set_ptr_x_absolute(lf_get_ptr_x() + 5.0f); // 5 is basically our margin to the left

        // allows you to cycle through prioties for tasks
        bool clicked_priority = lf_hovered((vec2s){lf_get_ptr_x(), lf_get_ptr_y()},
                                           (vec2s){priority_size, priority_size}) &&
                                lf_mouse_button_went_down(GLFW_MOUSE_BUTTON_LEFT);

        if (clicked_priority)
        {
            if (entry->priority + 1 >= PRIORITY_HIGH + 1)
            {
                entry->priority = 0;
            }
            else
            {
                entry->priority++;
            }
        }
        switch (entry->priority)
        {
        case PRIORITY_LOW:
        {
            lf_rect(priority_size, priority_size, (LfColor){76, 150, 80, 225}, 4.0f);
            break;
        }
        case PRIORITY_MEDIUM:
        {
            lf_rect(priority_size, priority_size, (LfColor){255, 235, 59, 225}, 4.0f);
            break;
        }
        case PRIORITY_HIGH:
        {
            lf_rect(priority_size, priority_size, (LfColor){244, 67, 54, 225}, 4.0f);
            break;
        }
        }
        lf_set_ptr_y_absolute(ptry_before);
        {
            LfUIElementProps props = lf_get_theme().button_props;
            props.color = LF_NO_COLOR;
            props.border_width = 0.0f;
            props.padding = 0.0f;
            props.margin_top = 0.0f;
            props.margin_left = 10.0f;
            lf_push_style_props(props);
            if (lf_image_button(((LfTexture){.id = removeTexture.id, .width = 20, .height = 20})) == LF_CLICKED)
            {
                for (uint32_t j = i; j < numEntries - 1; j++)
                {
                    entries[j] = entries[j + 1];
                }
                numEntries--;
            }
            lf_pop_style_props();
        }
        {
            LfUIElementProps props = lf_get_theme().checkbox_props;
            props.border_width = 1.0f;
            props.corner_radius = 0.0f;
            props.margin_top = 0.0f;
            props.padding = 5.0f;
            props.margin_left = 5.0f;
            props.color = lf_color_from_zto((vec4s){0.05f, 0.05f, 0.05f, 1.0f});
            lf_push_style_props(props);
            if (lf_checkbox("", &entry->completed, LF_NO_COLOR, ((LfColor){65, 167, 204, 255})) == LF_CLICKED)
            {
            }
            lf_pop_style_props();
        }
        lf_push_font(&smallfont);
        LfUIElementProps props = lf_get_theme().text_props;
        props.margin_top = 0.0f;
        props.margin_left = 5.0f;
        lf_push_style_props(props);
        float descptr_x = lf_get_ptr_x();
        lf_text(entry->desc);

        lf_set_ptr_x_absolute(descptr_x);
        lf_set_ptr_y_absolute(lf_get_ptr_y() + smallfont.font_size);
        props.text_color = (LfColor){150, 150, 150, 255};
        lf_push_style_props(props);
        lf_text(entry->date);
        lf_pop_style_props();
        lf_pop_font();

        lf_next_line();

        renderedCount++;
    }
    if (!renderedCount)
    {
        lf_text("there is no task here!, add some");
    }
    lf_div_end();
}

static void renderNewTask()
{
    lf_push_font(&titlefont);
    {
        LfUIElementProps props = lf_get_theme().text_props;
        props.margin_bottom = 15.0f;
        lf_push_style_props(props);
        lf_text("Add a new task");
        lf_pop_font();
    }
    lf_next_line();
    {
        lf_push_font(&smallfont);
        lf_text("description");
        lf_pop_font();

        lf_next_line();
        LfUIElementProps props = lf_get_theme().inputfield_props;
        props.padding = 15.0f;
        props.border_width = 0.0f;
        props.color = lf_color_from_zto((vec4s){0.05f, 0.05f, 0.05f, 1.0f});
        props.corner_radius = 11.0f;
        props.text_color = LF_WHITE;
        props.border_width = 1.0f;
        props.border_color = newTaskInput.selected ? LF_WHITE : (LfColor){170, 170, 170, 255};
        props.corner_radius = 2.5f;
        props.margin_bottom = 10.0f;
        lf_push_style_props(props);
        lf_input_text(&newTaskInput);
        lf_pop_style_props();
    }
    lf_next_line();

    static int32_t selectedPriority = -1;
    {
        lf_push_font(&smallfont);
        lf_text("priority");
        lf_pop_font();

        lf_next_line();
        static const char *items[3] = {
            "low",
            "medium",
            "high",
        }; // not sure if i can leave a trailing comma here

        static bool opened = false;
        LfUIElementProps props = lf_get_theme().button_props;
        props.color = (LfColor){70, 70, 70, 255};
        props.text_color = LF_WHITE;
        props.border_width = 0.0f;
        props.corner_radius = 5.0f;
        lf_push_style_props(props);
        lf_dropdown_menu(items, "priority", 3, 200, 80, &selectedPriority, &opened);
        lf_pop_style_props();
    }

    {
        // add new task button
        bool form_complete = (strlen(newTaskInput.buf) && selectedPriority != -1); // strlen will return a 1 if not empty since it's a char
        const char *text = "Add";
        const float width = 150.0f;

        LfUIElementProps props = lf_get_theme().button_props;
        props.margin_left = 0.0f;
        props.margin_right = 0.0f;
        props.corner_radius = 5.0f;
        props.border_width = 0.0f;
        props.color = !form_complete ? (LfColor){80, 80, 80, 255} : (LfColor){65, 167, 204, 255};
        lf_push_style_props(props);
        lf_set_line_should_overflow(false);
        lf_set_ptr_x_absolute(winw - (width + props.padding * 2.0f) - WIN_MARGIN);
        lf_set_ptr_y_absolute(winh - (lf_button_dimension(text).y + props.padding * 2.0f) - WIN_MARGIN);
        if (lf_button_fixed(text, width, -1) == LF_CLICKED && form_complete)
        {
            task_entry *entry = (task_entry *)malloc(sizeof(*entry));
            entry->priority = selectedPriority;
            entry->completed = false;
            entry->date = get_command_output("date +'%d,%m,%Y, %H:%M'");
            char *new_desc = malloc(strlen(newTaskInputBuf) + 1);
            strcpy(new_desc, newTaskInputBuf);
            entry->desc = new_desc;
            entries[numEntries++] = entry;
            memset(newTaskInputBuf, 0, 512);
            sortEntries();
            serialiseTodoList("./tododata.bin");
        }
        lf_set_line_should_overflow(true);
        lf_pop_style_props();
    }
    lf_next_line();
    {
        // back button
        LfUIElementProps props = lf_get_theme().button_props;
        props.color = LF_NO_COLOR;
        props.border_width = 0.0f;
        props.padding = 0.0f;
        props.margin_left = 0.0f;
        props.margin_top = 0.0f;
        props.margin_right = 0.0f;
        props.margin_bottom = 0.0f;
        lf_push_style_props(props);
        lf_set_line_should_overflow(false);
        LfTexture backbutton = (LfTexture){.id = backTexture.id, .width = 20, .height = 40};
        lf_set_ptr_y_absolute(winh - backbutton.height - WIN_MARGIN * 2.0f);
        lf_set_ptr_x_absolute(WIN_MARGIN);
        if (lf_image_button(backbutton) == LF_CLICKED)
        {
            current_tab = TAB_DASHBOARD;
        }
        lf_set_line_should_overflow(true);
        lf_pop_style_props();
    }
}

int main()
{
    glfwInit();

    GLFWwindow *window = glfwCreateWindow(winw, winh, "Todo", NULL, NULL);

    glfwMakeContextCurrent(window);
    lf_init_glfw(winw, winh, window);
    LfTheme theme = lf_get_theme();
    theme.div_props.color = LF_NO_COLOR;
    lf_set_theme(theme);
    titlefont = lf_load_font("./fonts/inter-bold.ttf", 40);
    smallfont = lf_load_font("./fonts/inter.ttf", 20);

    removeTexture = lf_load_texture("./icons/remove.png", true, LF_TEX_FILTER_LINEAR);
    backTexture = lf_load_texture("./icons/back.png", true, LF_TEX_FILTER_LINEAR);

    memset(newTaskInputBuf, 0, 512);
    newTaskInput = (LfInputField){
        .width = 400,
        .buf = newTaskInputBuf,
        .buf_size = 512,
        .placeholder = "What is there to do?"};

    deserialise_todo_list("./tododata.bin");
    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(0.05f, 0.05f, 0.05f, 0.05f);

        lf_begin();

        lf_div_begin(((vec2s){WIN_MARGIN, WIN_MARGIN}),
                     ((vec2s){winw - WIN_MARGIN * 2.0f, winh - WIN_MARGIN * 2.0f}),
                     true);

        switch (current_tab)
        case TAB_DASHBOARD:
        {
            rendertopbar();
            lf_next_line();

            renderFilters();
            lf_next_line();

            renderEntries();
            break;
        case TAB_NEW_TASK:
        {
            renderNewTask();
        }
        }

            lf_div_end();
        lf_end();

        glfwPollEvents();
        glfwSwapBuffers(window);
    }

    lf_free_font(&titlefont);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// gcc -o todo todo.c -lglfw -lGL
// gcc -o todo todo.c -lglfw -lGL -lleif -lclipboard -lm -lxcb