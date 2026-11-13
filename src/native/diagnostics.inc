// Read-only inspection, executed on the game's main thread when a diagnostic command is requested.
static void dumpRV(FILE* f,RV* r,int depth=0) {
    if(!r) {fputs("null",f);return;}
    if(r->numeric()) {double n=r->number();if(std::isfinite(n))fprintf(f,"%.17g",n);else fputs("null",f);return;}
    if((r->kind&0xffffff)==15) {fprintf(f,"{\"ref\":%u,\"type\":%u}",static_cast<unsigned>(r->value.integer),static_cast<unsigned>(r->value.integer>>32));return;}
    if((r->kind&0xffffff)==1 && r->value.ptr) {
        const char* s=*reinterpret_cast<const char**>(r->value.ptr);
        fputc('"',f);
        if(s) for(int i=0;s[i]&&i<2048;i++){unsigned char c=static_cast<unsigned char>(s[i]);if(c=='"'||c=='\\')fputc('\\',f);if(c>=32)fputc(c,f);else fprintf(f,"\\u%04x",c);}
        fputc('"',f);return;
    }
    if((r->kind&0xffffff)==2 && depth<4) {
        fputc('[',f);int size=std::min(array_size(r),256);
        for(int i=0;i<size;i++){if(i)fputc(',',f);dumpRV(f,array_at(r,i),depth+1);}fputc(']',f);return;
    }
    fputs("null",f);
}
static void dumpList(FILE* f,void* s,RV* list) {
    fputc('[',f);if(list&&list->handle())for(int i=0,n=listSize(s,nullptr,list);i<n;i++){if(i)fputc(',',f);RV x=listValue(s,nullptr,list,i);dumpRV(f,&x);}
    fputc(']',f);
}
static void diagnostics(void* self) {
    HMODULE module=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&diagnostics),&module);
    wchar_t path[MAX_PATH];GetModuleFileNameW(module,path,MAX_PATH);
    wchar_t* end=wcsrchr(path,L'\\');if(!end)return;
    swprintf_s(end,MAX_PATH-(end-path),L"\\diagnostics-%lu.json",GetCurrentProcessId());
    FILE* f=nullptr;if(_wfopen_s(&f,path,L"wb")!=0||!f){shared->testStatus=-1;return;}
    fputs("{\"items\":[",f);
    int items[]={10,12,32,905};
    for(int i=0;i<4;i++) {
        if(i)fputc(',',f);int item=items[i];RV key=RV::number(item);
        fprintf(f,"{\"id\":%d,\"info\":",item);dumpRV(f,array_at(global(offsets::var_ITEMINFO),item));
        RV recipe=call(self,nullptr,offsets::var_ds_map_find_value,{global(offsets::var_ITEM_RECIPES),&key});
        fputs(",\"recipe_id\":",f);dumpRV(f,&recipe);
        fputs(",\"recipe_info\":",f);dumpRV(f,recipe.numeric()?array_at(global(offsets::var_RECIPE_INFO),static_cast<int>(recipe.number())):nullptr);
        RV amount;RV* a[]={&key};reinterpret_cast<Script>(base+offsets::get_total_owned)(self,nullptr,&amount,1,a);
        fputs(",\"owned\":",f);dumpRV(f,&amount);fputc('}',f);
    }
    fputs("],\"stat_info\":",f);dumpRV(f,global(offsets::var_STATINFO));
    fputs(",\"stat_caps\":[",f);
    for(int stat=0;stat<36;stat++){if(stat)fputc(',',f);fprintf(f,"%.17g",nativeStatCap(stat));}
    fputc(']',f);
    fputs(",\"player_stats\":",f);dumpRV(f,global(offsets::var_PLAYERSTATS));
    fputs(",\"no_drop_recipes\":",f);dumpList(f,self,global(offsets::var_RECIPE_NODROP));
    fputs(",\"recipes_disabled\":",f);dumpRV(f,global(offsets::var_RECIPEDROPS_DISABLED));
    fputs(",\"station_data\":",f);dumpRV(f,global(offsets::var_STATION_DATA));
    RV grass=RV::number(12);RV candidates=call(self,nullptr,offsets::var_ds_map_find_value,{global(offsets::var_ITEM_RECIPE_DROPS),&grass});
    fputs(",\"sawgrass_recipes\":[",f);
    if(candidates.handle())for(int i=0,n=listSize(self,nullptr,&candidates);i<n;i++) {
        if(i)fputc(',',f);RV recipe=listValue(self,nullptr,&candidates,i);
        fputs("{\"recipe_id\":",f);dumpRV(f,&recipe);fputs(",\"info\":",f);dumpRV(f,recipe.numeric()?array_at(global(offsets::var_RECIPE_INFO),static_cast<int>(recipe.number())):nullptr);fputc('}',f);
    }
    fputs("],\"equipment\":[",f);
    RV* equipment=global(offsets::var_EQUIPMENT);bool first=true;
    for(int i=0;i<array_size(equipment);i++) {
        RV* entry=array_at(equipment,i);if(array_size(entry)<4)continue;
        if(!first)fputc(',',f);first=false;fprintf(f,"{\"index\":%d,\"data\":",i);dumpRV(f,entry);
        fputs(",\"stats\":",f);dumpList(f,self,array_at(entry,3));fputs(",\"values\":[",f);
        if(extremeEquipment(self,nullptr,i))for(int stat=0;stat<36;stat++){if(stat)fputc(',',f);RV eq=RV::number(i),st=RV::number(stat),result;RV* args[]={&eq,&st};hookEquipStat(self,nullptr,&result,2,args);dumpRV(f,&result);}
        fputs("]}",f);
    }
    fputs("]}",f);fclose(f);shared->testStatus=1;
}
