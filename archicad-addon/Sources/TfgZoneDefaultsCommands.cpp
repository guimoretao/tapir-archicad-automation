#include "TfgZoneDefaultsCommands.hpp"
#include "MigrationHelper.hpp"
#include "GSUnID.hpp"
#include "OnExit.hpp"

// TFG fork: Zone tool defaults (stamp library part and category).
//
// Why: CreateZonesCommand (ElementCreationCommands.cpp) starts every new Zone from
// ACAPI_Element_GetDefaults (&element, &memo) and only writes catInd when the item names
// categoryAttributeId. The zone stamp (API_ZoneType::libInd) and its parameters (memo.params)
// always come from the tool defaults, and changing the category does not change libInd. So
// when the defaults point at a stamp that is not in the loaded libraries (a project from an
// older template whose stamp, e.g. "Selo de Zona 01 27", is missing), every CreateZones item
// fails with the bare "Failed to create new Zone", whatever category it names. These commands
// show that state and let a script point the defaults at a loaded stamp.

static void SetZoneDefaultsHeader (API_Element& element)
{
    element = {};
#ifdef ServerMainVers_2600
    element.header.type = API_ZoneID;
#else
    element.header.typeID = API_ZoneID;
#endif
}

static GS::UniString MainGuidStringOfUnID (const char* ownUnID)
{
    if (ownUnID == nullptr || ownUnID[0] == '\0') {
        return GS::UniString ();
    }
    return APIGuidToString (GSGuid2APIGuid (GS::UnID (ownUnID).GetMainGuid ()));
}

// The library part at index, as an object; found: false when the index does not resolve.
static GS::ObjectState LibraryPartReport (Int32 index)
{
    GS::ObjectState report;
    report.Add ("index", index);
    if (index <= 0) {
        report.Add ("found", false);
        report.Add ("reason", GS::UniString ("libInd is not a valid library part index (0 or negative)."));
        return report;
    }
    API_LibPart libPart = {};
    libPart.index = index;
    const GSErrCode err = ACAPI_LibraryPart_Get (&libPart);
    GS::UniString path;
    if (libPart.location != nullptr) {
        libPart.location->ToPath (&path);
        delete libPart.location;
        libPart.location = nullptr;
    }
    if (err != NoError) {
        report.Add ("found", false);
        report.Add ("errorCode", static_cast<Int32> (err));
        report.Add ("reason", GS::UniString ("ACAPI_LibraryPart_Get failed for this index."));
        return report;
    }
    report.Add ("found", true);
    report.Add ("name", GS::UniString (libPart.docu_UName));
    report.Add ("fileName", GS::UniString (libPart.file_UName));
    report.Add ("ownUnID", GS::UniString (libPart.ownUnID));
    report.Add ("mainGuid", MainGuidStringOfUnID (libPart.ownUnID));
    report.Add ("parentUnID", GS::UniString (libPart.parentUnID));
    report.Add ("typeID", static_cast<Int32> (libPart.typeID));
    report.Add ("missingDef", libPart.missingDef);
    report.Add ("isPlaceable", libPart.isPlaceable);
    report.Add ("isTemplate", libPart.isTemplate);
    report.Add ("location", path);
    return report;
}

// The zone category attribute at catInd, with the stamp it names and whether that stamp is loaded.
static GS::ObjectState ZoneCategoryReport (const API_AttributeIndex& catInd)
{
    GS::ObjectState report;
    report.Add ("index", GetAttributeIndex (catInd));
    GS::UniString name;
    API_Attribute attr = {};
    attr.header.typeID = API_ZoneCatID;
    attr.header.index = catInd;
    attr.header.uniStringNamePtr = &name;
    const GSErrCode err = ACAPI_Attribute_Get (&attr);
    if (err != NoError) {
        report.Add ("found", false);
        report.Add ("errorCode", static_cast<Int32> (err));
        return report;
    }
    report.Add ("found", true);
    report.Add ("attributeId", CreateGuidObjectState (attr.header.guid));
    report.Add ("name", name);
    report.Add ("code", GS::UniString (attr.zoneCat.catCode));
    report.Add ("stampName", GS::UniString (attr.zoneCat.stampName));
    report.Add ("stampMainGuid", APIGuidToString (attr.zoneCat.stampMainGuid));
    report.Add ("stampRevGuid", APIGuidToString (attr.zoneCat.stampRevGuid));

    // Same lookup as GetZoneStampParameters (AttributeCommands.cpp): by unique ID and name.
    API_LibPart libPart = {};
    libPart.typeID = APILib_RoomID;
    const GS::UniString unIdStr = GS::UniString::Printf ("{%T}-{%T}",
        APIGuidToString (attr.zoneCat.stampMainGuid).ToPrintf (), APIGuidToString (attr.zoneCat.stampRevGuid).ToPrintf ());
    CHTruncate (unIdStr.ToCStr ().Get (), libPart.ownUnID, sizeof (libPart.ownUnID));
    GS::ucscpy (libPart.docu_UName, attr.zoneCat.stampName);
    const GSErrCode searchErr = ACAPI_LibraryPart_Search (&libPart, false, false);
    delete libPart.location;
    libPart.location = nullptr;
    const bool stampLoaded = searchErr == NoError && libPart.index > 0 && !libPart.missingDef &&
        GS::UnID (libPart.ownUnID).GetMainGuid () == APIGuid2GSGuid (attr.zoneCat.stampMainGuid);
    report.Add ("stampLoaded", stampLoaded);
    if (searchErr == NoError && libPart.index > 0) {
        report.Add ("stampSearchIndex", libPart.index);
        report.Add ("stampSearchName", GS::UniString (libPart.docu_UName));
        report.Add ("stampSearchMainGuid", MainGuidStringOfUnID (libPart.ownUnID));
    } else {
        report.Add ("stampSearchErrorCode", static_cast<Int32> (searchErr));
    }
    return report;
}

static GS::ObjectState ZoneDefaultsReport (GSErrCode& err)
{
    API_Element defaults;
    SetZoneDefaultsHeader (defaults);
    API_ElementMemo memo = {};
    const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });
    err = ACAPI_Element_GetDefaults (&defaults, &memo);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to read the Zone tool defaults.");
    }

    GS::ObjectState report;
    report.Add ("libInd", defaults.zone.libInd);
    report.Add ("stampLibraryPart", LibraryPartReport (defaults.zone.libInd));
    report.Add ("stampGuid", APIGuidToString (defaults.zone.stampGuid));
    report.Add ("category", ZoneCategoryReport (defaults.zone.catInd));

    const Int32 paramCount = memo.params != nullptr
        ? static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (memo.params)) / sizeof (API_AddParType))
        : 0;
    report.Add ("stampParameterCount", paramCount);

    GS::UniString layerName;
    API_Attribute layer = {};
    layer.header.typeID = API_LayerID;
    layer.header.index = defaults.header.layer;
    layer.header.uniStringNamePtr = &layerName;
    if (ACAPI_Attribute_Get (&layer) == NoError) {
        report.Add ("layerId", CreateGuidObjectState (layer.header.guid));
        report.Add ("layerName", layerName);
    }
    report.Add ("layerIndex", GetAttributeIndex (defaults.header.layer));
    report.Add ("floorIndex", static_cast<Int32> (defaults.header.floorInd));
    report.Add ("name", GS::UniString (defaults.zone.roomName));
    report.Add ("numberStr", GS::UniString (defaults.zone.roomNoStr));
    report.Add ("manual", defaults.zone.manual);
    report.Add ("pen", static_cast<Int32> (defaults.zone.pen));
    report.Add ("useStampPens", defaults.zone.useStampPens);
    report.Add ("roomHeight", defaults.zone.roomHeight);
    report.Add ("roomBaseLev", defaults.zone.roomBaseLev);
    report.Add ("roomFlThick", defaults.zone.roomFlThick);
    report.Add ("relativeTopStory", static_cast<Int32> (defaults.zone.relativeTopStory));
    report.Add ("floorFillIndex", GetAttributeIndex (defaults.zone.floorFillInd));
    report.Add ("useFloorFill", defaults.zone.useFloorFill);
    report.Add ("floorContLTypeIndex", GetAttributeIndex (defaults.zone.floorContLType));
    report.Add ("useContourLine", defaults.zone.useContourLine);
    report.Add ("materialIndex", GetAttributeIndex (defaults.zone.material));
    return report;
}

// Finds a loaded zone stamp by main guid (scan of every library part) or by name (search).
static GSErrCode FindStampLibraryPart (const GS::ObjectState& stamp, Int32& libInd, GS::UniString& errMessage)
{
    Int32 index = 0;
    GS::UniString guidStr;
    GS::UniString name;
    if (stamp.Get ("index", index)) {
        API_LibPart libPart = {};
        libPart.index = index;
        const GSErrCode err = ACAPI_LibraryPart_Get (&libPart);
        delete libPart.location;
        if (err != NoError || libPart.missingDef) {
            errMessage = GS::UniString::Printf ("No loaded library part with index %d.", index);
            return err != NoError ? err : APIERR_BADID;
        }
        libInd = index;
        return NoError;
    }
    if (stamp.Get ("guid", guidStr)) {
        guidStr.DeleteAll (GS::UniChar (char ('{')));
        guidStr.DeleteAll (GS::UniChar (char ('}')));
        const API_Guid mainGuid = APIGuidFromString (guidStr.ToCStr ());
        if (mainGuid == APINULLGuid) {
            errMessage = "stamp.guid is not a guid.";
            return APIERR_BADPARS;
        }
        Int32 partCount = 0;
        GSErrCode err = ACAPI_LibraryPart_GetNum (&partCount);
        if (err != NoError) {
            errMessage = "Failed to enumerate library parts.";
            return err;
        }
        for (Int32 i = 1; i <= partCount; ++i) {
            API_LibPart libPart = {};
            libPart.index = i;
            if (ACAPI_LibraryPart_Get (&libPart) != NoError) {
                continue;
            }
            delete libPart.location;
            if (libPart.missingDef) {
                continue;
            }
            if (GSGuid2APIGuid (GS::UnID (libPart.ownUnID).GetMainGuid ()) == mainGuid) {
                libInd = i;
                return NoError;
            }
        }
        errMessage = GS::UniString ("No loaded library part with main guid ") + guidStr + ".";
        return APIERR_BADID;
    }
    if (stamp.Get ("name", name) && !name.IsEmpty ()) {
        API_LibPart libPart = {};
        libPart.typeID = APILib_RoomID;
        const USize maxNameLength = sizeof (libPart.docu_UName) / sizeof (libPart.docu_UName[0]) - 1;
        GS::ucscpy (libPart.docu_UName, name.ToUStr (0, GS::Min (name.GetLength (), maxNameLength)).Get ());
        const GSErrCode err = ACAPI_LibraryPart_Search (&libPart, false, true);
        delete libPart.location;
        if (err != NoError || libPart.index <= 0 || libPart.missingDef) {
            errMessage = GS::UniString ("No loaded placeable zone stamp named '") + name + "'.";
            return err != NoError ? err : APIERR_BADID;
        }
        libInd = libPart.index;
        return NoError;
    }
    errMessage = "stamp needs one of index, guid or name.";
    return APIERR_BADPARS;
}

TfgGetZoneToolDefaultsCommand::TfgGetZoneToolDefaultsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String TfgGetZoneToolDefaultsCommand::GetName () const
{
    return "TfgGetZoneToolDefaults";
}

GS::Optional<GS::UniString> TfgGetZoneToolDefaultsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {},
        "additionalProperties": false
    })";
}

GS::Optional<GS::UniString> TfgGetZoneToolDefaultsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "description": "The Zone tool defaults: libInd and stampLibraryPart (the zone stamp new Zones get: index, found, name, fileName, ownUnID, mainGuid, missingDef, isPlaceable, location), stampGuid, category (index, attributeId, name, code, stampName, stampMainGuid, stampRevGuid, stampLoaded), stampParameterCount, layer, floorIndex, name, numberStr, manual and level fields.",
        "properties": {
            "libInd": { "type": "integer" },
            "stampLibraryPart": { "type": "object" },
            "stampGuid": { "type": "string" },
            "category": { "type": "object" },
            "stampParameterCount": { "type": "integer" }
        },
        "additionalProperties": true
    })";
}

GS::ObjectState TfgGetZoneToolDefaultsCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
    GSErrCode err = NoError;
    return ZoneDefaultsReport (err);
}

TfgSetZoneToolDefaultsCommand::TfgSetZoneToolDefaultsCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String TfgSetZoneToolDefaultsCommand::GetName () const
{
    return "TfgSetZoneToolDefaults";
}

GS::Optional<GS::UniString> TfgSetZoneToolDefaultsCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "stamp": {
                "type": "object",
                "description": "The zone stamp library part for the Zone tool defaults, by one of: index (library part index), guid (main guid, braces optional) or name (document name, searched among placeable zone stamps). Its default parameters replace the stamp parameters of the defaults.",
                "properties": {
                    "index": { "type": "integer" },
                    "guid": { "type": "string" },
                    "name": { "type": "string" }
                },
                "additionalProperties": false
            },
            "stampFromCategory": {
                "type": "boolean",
                "description": "Use the stamp named by the zone category (the given categoryAttributeId, or else the category of the defaults). Ignored when stamp is given."
            },
            "categoryAttributeId": {
                "$ref": "#/AttributeId",
                "description": "Optional zone category for the Zone tool defaults."
            }
        },
        "additionalProperties": false
    })";
}

GS::Optional<GS::UniString> TfgSetZoneToolDefaultsCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "description": "success, the libInd before and after, and the Zone tool defaults as TfgGetZoneToolDefaults reads them after the change (zoneToolDefaults).",
        "properties": {
            "success": { "type": "boolean" },
            "previousLibInd": { "type": "integer" },
            "zoneToolDefaults": { "type": "object" }
        },
        "additionalProperties": true
    })";
}

// The default parameters of the library part at libInd as a memo params handle (owned by the caller).
static GSErrCode GetStampParams (Int32 libInd, API_AddParType**& params)
{
    params = nullptr;
    double a = 0.0;
    double b = 0.0;
    Int32 addParNum = 0;
    const GSErrCode err = ACAPI_LibraryPart_GetParams (libInd, &a, &b, &addParNum, &params);
    if (err != NoError && params != nullptr) {
        ACAPI_DisposeAddParHdl (&params);
        params = nullptr;
    }
    return err;
}

// Resolves the stamp (stamp object, or the stamp of the category with stampFromCategory) and the
// optional category. libInd 0 = no stamp change asked.
static GS::Optional<GS::ObjectState> ResolveStampAndCategory (const GS::ObjectState& parameters, Int32& libInd, API_AttributeIndex& catInd, bool& hasCategory)
{
    libInd = 0;
    hasCategory = false;
    const GS::ObjectState* categoryId = parameters.Get ("categoryAttributeId");
    if (categoryId != nullptr) {
        catInd = GetAttributeIndexFromGuid (API_ZoneCatID, GetGuidFromObjectState (*categoryId));
        if (GetAttributeIndex (catInd) <= 0) {
            return CreateErrorResponse (APIERR_BADID, "Zone category not found for categoryAttributeId.");
        }
        hasCategory = true;
    }
    const GS::ObjectState* stamp = parameters.Get ("stamp");
    bool stampFromCategory = false;
    parameters.Get ("stampFromCategory", stampFromCategory);
    GS::UniString errMessage;
    if (stamp != nullptr) {
        const GSErrCode err = FindStampLibraryPart (*stamp, libInd, errMessage);
        if (err != NoError) {
            return CreateErrorResponse (err, errMessage);
        }
    } else if (stampFromCategory) {
        API_Attribute attr = {};
        attr.header.typeID = API_ZoneCatID;
        if (hasCategory) {
            attr.header.index = catInd;
        } else {
            API_Element defaults;
            SetZoneDefaultsHeader (defaults);
            const GSErrCode defErr = ACAPI_Element_GetDefaults (&defaults, nullptr);
            if (defErr != NoError) {
                return CreateErrorResponse (defErr, GS::UniString::Printf ("ACAPI_Element_GetDefaults (no memo) = %d", (int) defErr));
            }
            attr.header.index = defaults.zone.catInd;
        }
        const GSErrCode err = ACAPI_Attribute_Get (&attr);
        if (err != NoError) {
            return CreateErrorResponse (err, GS::UniString::Printf ("ACAPI_Attribute_Get (zone category) = %d", (int) err));
        }
        GS::ObjectState byGuid;
        byGuid.Add ("guid", APIGuidToString (attr.zoneCat.stampMainGuid));
        const GSErrCode findErr = FindStampLibraryPart (byGuid, libInd, errMessage);
        if (findErr != NoError) {
            return CreateErrorResponse (findErr, GS::UniString ("The stamp of the zone category is not loaded: ") + errMessage);
        }
    }
    return {};
}

// One way of writing the Zone tool defaults; every ACAPI code goes into the attempt record.
enum class SetStrategy { MemoNewParams, MemoNewParamsUndoable, FullMaskNewParamsUndoable, ParamsOnlyMemoUndoable, NullMemoUndoable };

static GSErrCode TrySetZoneDefaults (SetStrategy strategy, Int32 libInd, bool hasCategory, const API_AttributeIndex& catInd, GS::ObjectState& attempt)
{
    const char* names[] = { "memoNewParams", "memoNewParamsUndoable", "fullMaskNewParamsUndoable", "paramsOnlyMemoUndoable", "nullMemoUndoable" };
    attempt.Add ("strategy", GS::UniString (names[static_cast<int> (strategy)]));

    API_Element defaults;
    SetZoneDefaultsHeader (defaults);
    API_ElementMemo memo = {};
    const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });

    const bool wantsFullMemo = strategy == SetStrategy::MemoNewParams || strategy == SetStrategy::MemoNewParamsUndoable || strategy == SetStrategy::FullMaskNewParamsUndoable;
    GSErrCode err = NoError;
    if (wantsFullMemo) {
        err = ACAPI_Element_GetDefaults (&defaults, &memo);
        attempt.Add ("getDefaultsWithMemo", static_cast<Int32> (err));
        if (err != NoError) {
            ACAPI_DisposeElemMemoHdls (&memo);
            memo = {};
            SetZoneDefaultsHeader (defaults);
        }
    }
    if (!wantsFullMemo || err != NoError) {
        err = ACAPI_Element_GetDefaults (&defaults, nullptr);
        attempt.Add ("getDefaultsNoMemo", static_cast<Int32> (err));
        if (err != NoError) {
            return err;
        }
    }
    attempt.Add ("oldLibInd", defaults.zone.libInd);

    if (strategy != SetStrategy::NullMemoUndoable && libInd > 0) {
        API_AddParType** params = nullptr;
        err = GetStampParams (libInd, params);
        attempt.Add ("getParams", static_cast<Int32> (err));
        if (err != NoError) {
            return err;
        }
        if (memo.params != nullptr) {
            ACAPI_DisposeAddParHdl (&memo.params);
        }
        memo.params = params;
        attempt.Add ("newParamCount", static_cast<Int32> (BMGetHandleSize (reinterpret_cast<GSHandle> (params)) / sizeof (API_AddParType)));
    }

    API_Element mask = {};
    if (strategy == SetStrategy::FullMaskNewParamsUndoable) {
        ACAPI_ELEMENT_MASK_SETFULL (mask);
    } else {
        ACAPI_ELEMENT_MASK_CLEAR (mask);
    }
    if (libInd > 0) {
        defaults.zone.libInd = libInd;
        ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, libInd);
    }
    if (hasCategory) {
        defaults.zone.catInd = catInd;
        ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, catInd);
    }

    API_ElementMemo* memoPtr = (strategy == SetStrategy::NullMemoUndoable) ? nullptr : &memo;
    if (strategy == SetStrategy::MemoNewParams) {
        err = ACAPI_Element_ChangeDefaults (&defaults, memoPtr, &mask);
        attempt.Add ("changeDefaults", static_cast<Int32> (err));
        return err;
    }
    GSErrCode innerErr = NoError;
    const GSErrCode undoErr = ACAPI_CallUndoableCommand ("TFG Zone defaults", [&] () -> GSErrCode {
        innerErr = ACAPI_Element_ChangeDefaults (&defaults, memoPtr, &mask);
        return innerErr;
    });
    attempt.Add ("changeDefaults", static_cast<Int32> (innerErr));
    attempt.Add ("callUndoableCommand", static_cast<Int32> (undoErr));
    return innerErr != NoError ? innerErr : undoErr;
}

GS::ObjectState TfgSetZoneToolDefaultsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    Int32 libInd = 0;
    API_AttributeIndex catInd = {};
    bool hasCategory = false;
    const GS::Optional<GS::ObjectState> resolveError = ResolveStampAndCategory (parameters, libInd, catInd, hasCategory);
    if (resolveError.HasValue ()) {
        return resolveError.Get ();
    }
    if (libInd <= 0 && !hasCategory) {
        return CreateErrorResponse (APIERR_BADPARS, "Nothing to change: give stamp, stampFromCategory or categoryAttributeId.");
    }

    GS::Array<GS::ObjectState> attempts;
    const SetStrategy strategies[] = { SetStrategy::MemoNewParams, SetStrategy::MemoNewParamsUndoable, SetStrategy::FullMaskNewParamsUndoable, SetStrategy::ParamsOnlyMemoUndoable, SetStrategy::NullMemoUndoable };
    GSErrCode lastErr = APIERR_GENERAL;
    bool success = false;
    GS::UniString summary;
    for (const SetStrategy strategy : strategies) {
        GS::ObjectState attempt;
        lastErr = TrySetZoneDefaults (strategy, libInd, hasCategory, catInd, attempt);
        attempt.Add ("result", static_cast<Int32> (lastErr));
        attempts.Push (attempt);
        summary += GS::UniString::Printf ("[%d]=%d ", (int) attempts.GetSize (), (int) lastErr);
        if (lastErr == NoError) {
            success = true;
            break;
        }
    }

    GS::ObjectState response = success
        ? GS::ObjectState ("success", true)
        : CreateErrorResponse (lastErr, GS::UniString ("ACAPI_Element_ChangeDefaults refused the change of the Zone tool defaults in every strategy: ") + summary + "(see attempts).");
    response.Add ("newLibInd", libInd);
    response.Add ("attempts", attempts);
    GSErrCode readErr = NoError;
    response.Add ("zoneToolDefaults", ZoneDefaultsReport (readErr));
    return response;
}

TfgCreateZoneWithStampCommand::TfgCreateZoneWithStampCommand () :
    CommandBase (CommonSchema::Used)
{
}

GS::String TfgCreateZoneWithStampCommand::GetName () const
{
    return "TfgCreateZoneWithStamp";
}

GS::Optional<GS::UniString> TfgCreateZoneWithStampCommand::GetInputParametersSchema () const
{
    return R"({
        "type": "object",
        "properties": {
            "stamp": {
                "type": "object",
                "description": "The zone stamp library part, by index, main guid or name (as in TfgSetZoneToolDefaults).",
                "properties": {
                    "index": { "type": "integer" },
                    "guid": { "type": "string" },
                    "name": { "type": "string" }
                },
                "additionalProperties": false
            },
            "stampFromCategory": { "type": "boolean", "description": "Use the stamp of the zone category (categoryAttributeId, else the default category)." },
            "categoryAttributeId": { "$ref": "#/AttributeId" },
            "name": { "type": "string" },
            "numberStr": { "type": "string" },
            "floorIndex": { "type": "integer" },
            "polygonCoordinates": {
                "type": "array",
                "description": "Outline of the manual zone (no arcs, no holes), at least 3 points.",
                "items": { "$ref": "#/Coordinate2D" },
                "minItems": 3
            },
            "stampPosition": { "$ref": "#/Coordinate2D" }
        },
        "additionalProperties": false,
        "required": [ "polygonCoordinates" ]
    })";
}

GS::Optional<GS::UniString> TfgCreateZoneWithStampCommand::GetRawResponseSchema () const
{
    return R"({
        "type": "object",
        "description": "elementId of the new Zone (or error) and the code of every ACAPI call (codes).",
        "properties": {
            "elementId": { "$ref": "#/ElementId" },
            "codes": { "type": "object" }
        },
        "additionalProperties": true
    })";
}

GS::ObjectState TfgCreateZoneWithStampCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    Int32 libInd = 0;
    API_AttributeIndex catInd = {};
    bool hasCategory = false;
    const GS::Optional<GS::ObjectState> resolveError = ResolveStampAndCategory (parameters, libInd, catInd, hasCategory);
    if (resolveError.HasValue ()) {
        return resolveError.Get ();
    }
    if (libInd <= 0) {
        return CreateErrorResponse (APIERR_BADPARS, "Give stamp or stampFromCategory: the zone is created with that stamp.");
    }

    GS::Array<GS::ObjectState> polygonCoordinates;
    parameters.Get ("polygonCoordinates", polygonCoordinates);
    if (polygonCoordinates.GetSize () >= 2 && IsSame2DCoordinate (polygonCoordinates.GetFirst (), polygonCoordinates.GetLast ())) {
        polygonCoordinates.Pop ();
    }
    if (polygonCoordinates.GetSize () < 3) {
        return CreateErrorResponse (APIERR_BADPARS, "polygonCoordinates needs at least 3 distinct points.");
    }

    GS::ObjectState codes;
    API_Element element;
    SetZoneDefaultsHeader (element);
    API_ElementMemo memo = {};
    const GS::OnExit guard ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });

    GSErrCode err = ACAPI_Element_GetDefaults (&element, &memo);
    codes.Add ("getDefaultsWithMemo", static_cast<Int32> (err));
    if (err != NoError) {
        ACAPI_DisposeElemMemoHdls (&memo);
        memo = {};
        SetZoneDefaultsHeader (element);
        err = ACAPI_Element_GetDefaults (&element, nullptr);
        codes.Add ("getDefaultsNoMemo", static_cast<Int32> (err));
        if (err != NoError) {
            GS::ObjectState response = CreateErrorResponse (err, GS::UniString::Printf ("ACAPI_Element_GetDefaults = %d", (int) err));
            response.Add ("codes", codes);
            return response;
        }
    }
    codes.Add ("defaultsLibInd", element.zone.libInd);

    // Only the outline: the memo of the defaults may carry other handles, drop them all.
    API_AddParType** params = nullptr;
    err = GetStampParams (libInd, params);
    codes.Add ("getParams", static_cast<Int32> (err));
    if (err != NoError) {
        GS::ObjectState response = CreateErrorResponse (err, GS::UniString::Printf ("ACAPI_LibraryPart_GetParams (%d) = %d", (int) libInd, (int) err));
        response.Add ("codes", codes);
        return response;
    }
    ACAPI_DisposeElemMemoHdls (&memo);
    memo = {};
    memo.params = params;

    element.zone.libInd = libInd;
    if (hasCategory) {
        element.zone.catInd = catInd;
    }
    parameters.Get ("floorIndex", element.header.floorInd);
    SetUCharProperty (&parameters, "name", element.zone.roomName);
    SetUCharProperty (&parameters, "numberStr", element.zone.roomNoStr);
    element.zone.manual = true;
    element.zone.poly.nCoords = polygonCoordinates.GetSize () + 1;
    element.zone.poly.nSubPolys = 1;
    element.zone.poly.nArcs = 0;
    memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.zone.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
    memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.zone.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
    memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (0, ALLOCATE_CLEAR, 0));
    Int32 iCoord = 1;
    Int32 iArc = 0;
    Int32 iPends = 1;
    AddPolyToMemo (polygonCoordinates, GS::Array<GS::ObjectState> (), iCoord, iArc, iPends, memo);

    const GS::ObjectState* stampPosition = parameters.Get ("stampPosition");
    if (stampPosition != nullptr) {
        element.zone.pos = Get2DCoordinateFromObjectState (*stampPosition);
    } else {
        API_Coord centre = { 0.0, 0.0 };
        for (const GS::ObjectState& c : polygonCoordinates) {
            const API_Coord p = Get2DCoordinateFromObjectState (c);
            centre.x += p.x;
            centre.y += p.y;
        }
        centre.x /= polygonCoordinates.GetSize ();
        centre.y /= polygonCoordinates.GetSize ();
        element.zone.pos = centre;
    }
    element.zone.refPos = element.zone.pos;

    GSErrCode createErr = NoError;
    const GSErrCode undoErr = ACAPI_CallUndoableCommand ("TFG Create Zone", [&] () -> GSErrCode {
        createErr = ACAPI_Element_Create (&element, &memo);
        return createErr;
    });
    codes.Add ("elementCreate", static_cast<Int32> (createErr));
    codes.Add ("callUndoableCommand", static_cast<Int32> (undoErr));

    GS::ObjectState response;
    if (createErr != NoError || undoErr != NoError) {
        const GSErrCode e = createErr != NoError ? createErr : undoErr;
        response = CreateErrorResponse (e, GS::UniString::Printf ("ACAPI_Element_Create = %d, ACAPI_CallUndoableCommand = %d", (int) createErr, (int) undoErr));
    } else {
        response.Add ("elementId", CreateGuidObjectState (element.header.guid));
    }
    response.Add ("libInd", libInd);
    response.Add ("codes", codes);
    return response;
}
