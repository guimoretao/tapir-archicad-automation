#pragma once

#include "CommandBase.hpp"

// TFG fork (guimoretao/tapir-archicad-automation, branch tfg/zone-defaults).
// Reads and writes the zone stamp of the Zone tool defaults (API_ZoneType::libInd and
// catInd), which the public Tapir commands do not expose. Diagnoses CreateZones answering
// "Failed to create new Zone" in a project whose Zone defaults point at a missing stamp.

class TfgGetZoneToolDefaultsCommand : public CommandBase
{
public:
    TfgGetZoneToolDefaultsCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};

class TfgSetZoneToolDefaultsCommand : public CommandBase
{
public:
    TfgSetZoneToolDefaultsCommand ();
    virtual GS::String GetName () const override;
    virtual GS::Optional<GS::UniString> GetInputParametersSchema () const override;
    virtual GS::Optional<GS::UniString> GetRawResponseSchema () const override;
    virtual GS::ObjectState Execute (const GS::ObjectState& parameters, GS::ProcessControl& processControl) const override;
};
