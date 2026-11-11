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

namespace TapirCommand {

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

GS::ObjectState TfgSetZoneToolDefaultsCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
    API_Element defaults;
    SetZoneDefaultsHeader (defaults);
    GSErrCode err = ACAPI_Element_GetDefaults (&defaults, nullptr);
    if (err != NoError) {
        return CreateErrorResponse (err, "Failed to read the Zone tool defaults.");
    }
    const Int32 previousLibInd = defaults.zone.libInd;

    API_Element mask = {};
    ACAPI_ELEMENT_MASK_CLEAR (mask);
    bool anyChange = false;

    const GS::ObjectState* categoryId = parameters.Get ("categoryAttributeId");
    if (categoryId != nullptr) {
        const API_AttributeIndex catInd = GetAttributeIndexFromGuid (API_ZoneCatID, GetGuidFromObjectState (*categoryId));
        if (GetAttributeIndex (catInd) <= 0) {
            return CreateErrorResponse (APIERR_BADID, "Zone category not found for categoryAttributeId.");
        }
        defaults.zone.catInd = catInd;
        ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, catInd);
        anyChange = true;
    }

    Int32 newLibInd = 0;
    const GS::ObjectState* stamp = parameters.Get ("stamp");
    bool stampFromCategory = false;
    parameters.Get ("stampFromCategory", stampFromCategory);
    if (stamp != nullptr) {
        GS::UniString errMessage;
        err = FindStampLibraryPart (*stamp, newLibInd, errMessage);
        if (err != NoError) {
            return CreateErrorResponse (err, errMessage);
        }
    } else if (stampFromCategory) {
        API_Attribute attr = {};
        attr.header.typeID = API_ZoneCatID;
        attr.header.index = defaults.zone.catInd;
        err = ACAPI_Attribute_Get (&attr);
        if (err != NoError) {
            return CreateErrorResponse (err, "Failed to read the zone category.");
        }
        GS::ObjectState byGuid;
        byGuid.Add ("guid", APIGuidToString (attr.zoneCat.stampMainGuid));
        GS::UniString errMessage;
        err = FindStampLibraryPart (byGuid, newLibInd, errMessage);
        if (err != NoError) {
            return CreateErrorResponse (err, GS::UniString ("The stamp of the zone category is not loaded: ") + errMessage);
        }
    }
    if (newLibInd > 0) {
        defaults.zone.libInd = newLibInd;
        ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, libInd);
        anyChange = true;
    }

    if (!anyChange) {
        return CreateErrorResponse (APIERR_BADPARS, "Nothing to change: give stamp, stampFromCategory or categoryAttributeId.");
    }

    // memo nullptr: for a library part based default Archicad then takes the default
    // parameters of the (new) library part (ACAPI_Element_ChangeDefaults remarks).
    err = ACAPI_Element_ChangeDefaults (&defaults, nullptr, &mask);
    if (err != NoError) {
        return CreateErrorResponse (err, "ACAPI_Element_ChangeDefaults refused the change of the Zone tool defaults.");
    }

    GSErrCode readErr = NoError;
    GS::ObjectState response;
    response.Add ("success", true);
    response.Add ("previousLibInd", previousLibInd);
    response.Add ("zoneToolDefaults", ZoneDefaultsReport (readErr));
    return response;
}

}
