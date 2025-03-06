from dataclasses import dataclass
from Options import PerGameCommonOptions, DeathLink, Toggle, Choice, ExcludeLocations

class NoWeaponRequirements(Toggle):
    """Remove the requirements to wield weapons"""
    display_name = "No Weapon Requirements"

class EnableDLCsOption(Toggle):
    """Include items and locations exclusive to the DLCs"""
    display_name = "Enable DLCs"
    
class EnableNGPOption(Toggle):
    """Include items and locations exclusive to NewGame+ cycles"""
    display_name = "Enable NewGame+"

class GameVersion(Choice):
    """Choose the game version you will be playing on
     
    - **sotfs:** You will be playing the scholar of the first sin version
    - **vanilla:** You will be playing the vanilla version
    """
    display_name = "Game Version"
    option_sotfs = 0
    option_vanilla = 1

class KeepInfiniteLifegems(Toggle):
    """Keep Melentia's infinite supply of lifegems"""
    display_name = "Keep Infinite Lifegems"

class DS2ExcludeLocations(ExcludeLocations):
    """Prevent these locations from having an important item."""
    default = frozenset({"Chasm of the Abyss", "Dragon Memories"})

@dataclass
class DS2Options(PerGameCommonOptions):
    game_version: GameVersion
    death_link: DeathLink
    no_weapon_req: NoWeaponRequirements
    enable_dlcs: EnableDLCsOption
    enable_ngp: EnableNGPOption
    infinite_lifegems: KeepInfiniteLifegems
    exclude_locations: DS2ExcludeLocations