typedef void lv_timer_t;
static object energy_view,energy_container,energy_title,energy_text,energy_context,energy_retry,settings_view;
static object *energy_settings_screen=&energy_view,*energy_settings_list=&energy_container;
static object *energy_settings_heading=&energy_title,*energy_settings_note=&energy_text;
static object *energy_settings_context=&energy_context,*energy_settings_retry=&energy_retry,*settings_screen=&settings_view;
static object *energy_rows[8];
static bool energy_rows_updating;
static dashboard_energy_catalog_t *energy_ui_catalog;
static dashboard_energy_selection_t energy_draft;
static dashboard_energy_catalog_kind_t energy_stage;
static uint32_t energy_request,energy_loaded;
static dashboard_data_snapshot_t snapshot;
static dashboard_energy_catalog_t response;
static dashboard_data_state_t response_state=DASH_DATA_FRESH;
static uint32_t requested,cancelled;
static int saves;
static bool save_failure;
static bool fail_ui_alloc;
static void *energy_test_calloc(size_t count,size_t size) { return fail_ui_alloc ? NULL : calloc(count,size); }
#define calloc energy_test_calloc
static object *lv_screen_active(void) { return active; }
void dashboard_data_snapshot(dashboard_data_snapshot_t *out) { *out=snapshot; }
uint32_t dashboard_data_energy_catalog_request(dashboard_energy_catalog_kind_t k,const char *a,const char *o)
{ return ++requested; }
bool dashboard_data_energy_catalog_copy(uint32_t generation,dashboard_energy_catalog_t *out,dashboard_data_state_t *state)
{ if(generation!=requested) return false; *state=response_state; if(out) *out=response; return true; }
void dashboard_data_energy_catalog_cancel(void) { ++cancelled; }
esp_err_t dashboard_data_select_energy(const dashboard_energy_selection_t *s)
{ if(save_failure) return ESP_FAIL; ++saves; snapshot.energy_selection=*s; return ESP_OK; }
int dashboard_energy_area_hint(const dashboard_energy_catalog_t *c,const char *s) { return -1; }
static void update_energy_settings(lv_timer_t *timer);

/* INSERT WIZARD */

int main(void)
{
    selected_city=(const dashboard_city_t *)&(dashboard_city_t){.id="UA12020010010037010"};
    snapshot.energy_selection=(dashboard_energy_selection_t){.area="old",.area_name="Стара область",.initiator="old-op",.supplier_name="Старий оператор",.queue="5.2"};
    for(unsigned cycle=0;cycle<3;++cycle) {
        response.count=27;
        for(unsigned i=0;i<27;++i) {
            snprintf(response.entries[i].id,sizeof(response.entries[i].id),"area-%u",i);
            snprintf(response.entries[i].name,sizeof(response.entries[i].name),"Область %u",i);
        }
        open_energy_settings(NULL); update_energy_settings(NULL);
        assert(energy_settings_list->count<=9 && saves==cycle);
        energy_settings_list->scroll=12*58;refresh_energy_rows(NULL);
        assert(!strcmp(((dashboard_energy_entry_t *)energy_rows[0]->data)->id,"area-12"));
        lv_event_t touch={.target=energy_rows[0]}; select_energy_row(&touch);
        assert(!strcmp(energy_draft.area,"area-12") && saves==cycle);
        select_energy_row(&touch); /* A queued tap on a row hidden during loading is ignored. */
        assert(energy_stage==DASH_ENERGY_SUPPLIERS && saves==cycle);
        response.count=1; strcpy(response.entries[0].id,"dtek"); strcpy(response.entries[0].name,"ДТЕК");
        update_energy_settings(NULL);touch.target=energy_rows[0]; select_energy_row(&touch);
        assert(!strcmp(energy_draft.initiator,"dtek") && saves==cycle);
        response.count=1;strcpy(response.entries[0].id,"1.1");strcpy(response.entries[0].name,"1.1");
        update_energy_settings(NULL);touch.target=energy_rows[0];save_failure=true;
        select_energy_row(&touch);assert(saves==cycle && active==energy_settings_screen);
        save_failure=false;select_energy_row(&touch);
        assert(saves==cycle+1 && active==home_screen && !strcmp(snapshot.energy_selection.queue,"1.1"));
        update_energy_settings(NULL);assert(!energy_ui_catalog && !energy_settings_list->count);
    }
    open_energy_settings(NULL); energy_back(NULL);
    assert(active==settings_screen && saves==3);
    update_energy_settings(NULL);
    assert(!energy_ui_catalog && cancelled);
    fail_ui_alloc=true;
    open_energy_settings(NULL);
    assert(active==energy_settings_screen && !energy_request && *energy_settings_note->text);
    fail_ui_alloc=false; retry_energy_catalog(NULL);
    assert(energy_ui_catalog && energy_request);
    energy_back(NULL); update_energy_settings(NULL);
    puts("energy wizard callbacks, recycled bindings, bounded rows, save failure and cleanup: PASS");
    return 0;
}
