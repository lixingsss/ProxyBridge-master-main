// ui/slots.h - Proxy Slot Monitor tab
// Shows real-time rotation slots, active PIDs, process names, and idle status grouped by proxy.
#ifndef PB_UI_SLOTS_H
#define PB_UI_SLOTS_H

#define IDC_SLOTS         3020
#define IDC_SLOTS_REFRESH 3021
#define TIMER_SLOTS       3

#ifndef LVGS_COLLAPSIBLE
#define LVGS_COLLAPSIBLE 0x00000008
#endif

static HWND g_hSlots = NULL;
static HWND g_hSlotsRefresh = NULL;
static BOOL g_slotsActive = FALSE;

static void DarkListView(HWND lv, DWORD extraExStyle);        // dialogs.h

static const int g_slotColStr[6] = {
    S_SLOT_COL_SLOT, S_SLOT_COL_APP, S_SLOT_COL_PID, S_SLOT_COL_STATUS, S_SLOT_COL_RULE, S_COL_CFG
};

static void SlotsSetColumns(void)
{
    for (int i = 0; i < 6; i++)
    {
        LVCOLUMNW c; c.mask = LVCF_TEXT; c.pszText = (LPWSTR)T(g_slotColStr[i]);
        ListView_SetColumn(g_hSlots, i, &c);
    }
}

static HWND SlotsCreate(HWND parent)
{
    g_hSlots = CreateWindowExW(0, WC_LISTVIEWW, NULL,
                               WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
                               0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_SLOTS, g_hInst, NULL);
    if (!g_hSlots) return NULL;
    DarkListView(g_hSlots, LVS_EX_LABELTIP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_EnableGroupView(g_hSlots, TRUE);

    static const int w[6] = { 100, 190, 80, 95, 150, 240 };
    LVCOLUMNW c; c.mask = LVCF_TEXT | LVCF_WIDTH;
    for (int i = 0; i < 6; i++)
    {
        c.pszText = (LPWSTR)T(g_slotColStr[i]);
        c.cx = Sc(w[i]);
        ListView_InsertColumn(g_hSlots, i, &c);
    }

    g_hSlotsRefresh = CreateWindowExW(0, L"BUTTON", T(S_SLOT_REFRESH),
                                      WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                                      0, 0, 0, 0, parent, (HMENU)(UINT_PTR)IDC_SLOTS_REFRESH, g_hInst, NULL);
    return g_hSlots;
}

static void SlotsRefresh(void)
{
    if (!g_hSlots) return;

    SendMessageW(g_hSlots, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hSlots);
    ListView_RemoveAllGroups(g_hSlots);

    PBRotationAlloc allocs[PB_ROTATION_ALLOCS_MAX];
    UINT32 allocCount = 0;
    if (g_api.GetRotationAllocations)
        allocCount = g_api.GetRotationAllocations(allocs, PB_ROTATION_ALLOCS_MAX);

    int totalGroups = 0;
    int itemIdx = 0;

    // Iterate through profile rules to find rotation rules
    for (int rIdx = 0; rIdx < g_profile.ruleCount; rIdx++)
    {
        const PBRule* rule = &g_profile.rule[rIdx];
        if (!rule->enabled || !rule->rotationEnabled || rule->rotSlotCount <= 0)
            continue;

        for (int sIdx = 0; sIdx < rule->rotSlotCount; sIdx++)
        {
            UINT32 storedId = rule->rotSlot[sIdx].storedId;
            UINT32 quota = rule->rotSlot[sIdx].count > 0 ? rule->rotSlot[sIdx].count : 1;

            // Find matching proxy config in profile
            const PBConfig* cfg = NULL;
            for (int p = 0; p < g_profile.cfgCount; p++)
            {
                if (g_profile.cfg[p].storedId == storedId) {
                    cfg = &g_profile.cfg[p];
                    break;
                }
            }

            UINT32 nativeProxyId = cfg ? cfg->nativeId : 0;

            // Find how many allocs belong to this (rule, proxy)
            PBRotationAlloc matchedAllocs[64];
            int matchedCount = 0;
            for (UINT32 a = 0; a < allocCount && matchedCount < 64; a++)
            {
                if (allocs[a].rule_id == rule->nativeId &&
                    (nativeProxyId != 0 ? allocs[a].proxy_config_id == nativeProxyId : TRUE))
                {
                    matchedAllocs[matchedCount++] = allocs[a];
                }
            }

            int occupied = matchedCount;
            int idle = (int)quota > occupied ? ((int)quota - occupied) : 0;
            BOOL used[64] = { FALSE };

            // Build Group Header
            wchar_t grpHeader[256];
            wchar_t proxyDesc[128];
            if (cfg)
            {
                swprintf_s(proxyDesc, 128, L"%s (%s:%s)",
                           cfg->name[0] ? cfg->name : cfg->type,
                           cfg->host, cfg->port);
            }
            else
            {
                swprintf_s(proxyDesc, 128, L"Proxy #%u", storedId);
            }

            const wchar_t* rName = rule->name[0] ? rule->name : rule->proc;
            swprintf_s(grpHeader, 256,
                       L"%s  [%s]  --  %s: %u  |  %s: %d  |  %s: %d",
                       proxyDesc, rName,
                       T(S_COL_PROCS), quota,
                       T(S_SLOT_STATUS_BUSY), occupied,
                       T(S_SLOT_STATUS_IDLE), idle);

            int groupId = totalGroups + 1;
            LVGROUP grp;
            ZeroMemory(&grp, sizeof(grp));
            grp.cbSize = sizeof(grp);
            grp.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
            grp.state = LVGS_COLLAPSIBLE;
            grp.iGroupId = groupId;
            grp.pszHeader = grpHeader;
            ListView_InsertGroup(g_hSlots, -1, &grp);
            totalGroups++;

            // Insert rows for each slot in this quota (bound by slot_index == k)
            for (UINT32 k = 0; k < quota; k++)
            {
                wchar_t slotTxt[32];
                swprintf_s(slotTxt, 32, L"#%u", k + 1);

                wchar_t procTxt[MAX_PATH] = L"-";
                wchar_t pidTxt[32] = L"-";
                const wchar_t* statusTxt = T(S_SLOT_STATUS_IDLE);

                // Find process claiming this slot_index
                PBRotationAlloc* matched = NULL;
                for (int m = 0; m < matchedCount; m++)
                {
                    if (matchedAllocs[m].slot_index == k && !used[m])
                    {
                        matched = &matchedAllocs[m];
                        used[m] = TRUE;
                        break;
                    }
                }

                if (matched)
                {
                    int n = MultiByteToWideChar(CP_UTF8, 0, matched->process_name, -1, procTxt, MAX_PATH);
                    if (n <= 0) wcscpy_s(procTxt, MAX_PATH, L"-");
                    swprintf_s(pidTxt, 32, L"%lu", matched->pid);
                    statusTxt = T(S_SLOT_STATUS_BUSY);
                }

                LVITEMW it;
                ZeroMemory(&it, sizeof(it));
                it.mask = LVIF_TEXT | LVIF_GROUPID;
                it.iItem = itemIdx++;
                it.iSubItem = 0;
                it.pszText = slotTxt;
                it.iGroupId = groupId;
                int row = ListView_InsertItem(g_hSlots, &it);

                ListView_SetItemText(g_hSlots, row, 1, procTxt);
                ListView_SetItemText(g_hSlots, row, 2, pidTxt);
                ListView_SetItemText(g_hSlots, row, 3, (LPWSTR)statusTxt);
                ListView_SetItemText(g_hSlots, row, 4, (LPWSTR)rName);
                ListView_SetItemText(g_hSlots, row, 5, proxyDesc);
            }

            // Also show any overflow processes exceeding quota
            for (int m = 0; m < matchedCount; m++)
            {
                if (!used[m])
                {
                    wchar_t slotTxt[32];
                    swprintf_s(slotTxt, 32, L"#%u", matchedAllocs[m].slot_index + 1);

                    wchar_t procTxt[MAX_PATH] = L"-";
                    wchar_t pidTxt[32] = L"-";
                    int n = MultiByteToWideChar(CP_UTF8, 0, matchedAllocs[m].process_name, -1, procTxt, MAX_PATH);
                    if (n <= 0) wcscpy_s(procTxt, MAX_PATH, L"-");
                    swprintf_s(pidTxt, 32, L"%lu", matchedAllocs[m].pid);
                    const wchar_t* statusTxt = T(S_SLOT_STATUS_BUSY);

                    LVITEMW it;
                    ZeroMemory(&it, sizeof(it));
                    it.mask = LVIF_TEXT | LVIF_GROUPID;
                    it.iItem = itemIdx++;
                    it.iSubItem = 0;
                    it.pszText = slotTxt;
                    it.iGroupId = groupId;
                    int row = ListView_InsertItem(g_hSlots, &it);

                    ListView_SetItemText(g_hSlots, row, 1, procTxt);
                    ListView_SetItemText(g_hSlots, row, 2, pidTxt);
                    ListView_SetItemText(g_hSlots, row, 3, (LPWSTR)statusTxt);
                    ListView_SetItemText(g_hSlots, row, 4, (LPWSTR)rName);
                    ListView_SetItemText(g_hSlots, row, 5, proxyDesc);
                }
            }
        }
    }

    // If no rotation rules exist, show an informational group
    if (totalGroups == 0)
    {
        LVGROUP grp;
        ZeroMemory(&grp, sizeof(grp));
        grp.cbSize = sizeof(grp);
        grp.mask = LVGF_HEADER | LVGF_GROUPID;
        grp.iGroupId = 1;
        grp.pszHeader = (LPWSTR)T(S_SLOT_NO_RULES);
        ListView_InsertGroup(g_hSlots, -1, &grp);

        LVITEMW it;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_GROUPID;
        it.iItem = 0;
        it.pszText = L"-";
        it.iGroupId = 1;
        int row = ListView_InsertItem(g_hSlots, &it);
        ListView_SetItemText(g_hSlots, row, 1, (LPWSTR)T(S_SLOT_NO_RULES));
        ListView_SetItemText(g_hSlots, row, 2, L"-");
        ListView_SetItemText(g_hSlots, row, 3, (LPWSTR)T(S_SLOT_STATUS_IDLE));
        ListView_SetItemText(g_hSlots, row, 4, L"-");
        ListView_SetItemText(g_hSlots, row, 5, L"-");
    }

    SendMessageW(g_hSlots, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hSlots, NULL, TRUE);
}

static void SlotsActivate(HWND main, BOOL active)
{
    g_slotsActive = active;
    if (active)
    {
        SlotsRefresh();
        SetTimer(main, TIMER_SLOTS, 1000, NULL);
    }
    else
    {
        KillTimer(main, TIMER_SLOTS);
    }
}

static LRESULT SlotsNotify(LPARAM lp)
{
    NMHDR* nh = (NMHDR*)lp;
    if (nh->code == NM_CUSTOMDRAW)
    {
        NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
        {
            wchar_t status[32] = {0};
            ListView_GetItemText(g_hSlots, (int)cd->nmcd.dwItemSpec, 3, status, 32);
            if (!_wcsicmp(status, T(S_SLOT_STATUS_BUSY)))
                cd->clrText = C_ACCENT; // vibrant blue for occupied slots
            else
                cd->clrText = C_DIM;    // muted gray for idle slots
            return CDRF_NEWFONT;
        }
        return CDRF_DODEFAULT;
    }
    return 0;
}

#endif // PB_UI_SLOTS_H
