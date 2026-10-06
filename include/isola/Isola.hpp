#pragma once

/// Isola - a small OpenGL game engine: procedural islands with oceans, forests and villages,
/// the Sponza palace, and a first-person player.
///
/// Include this header for the whole public API:
///   isola::Engine          the engine (window, world, player, main loop)
///   isola::EngineConfig    start-up settings
///   isola::Map             the available worlds
///   isola::TerrainParams, isola::FoliageParams, isola::GraphicsSettings, isola::SkySettings, isola::SunSettings
///   isola::Key             keyboard keys for input callbacks
///   isola::Error           the exception type

#include <isola/Config.hpp>
#include <isola/Engine.hpp>
#include <isola/Error.hpp>
#include <isola/Foliage.hpp>
#include <isola/Graphics.hpp>
#include <isola/Input.hpp>
#include <isola/Map.hpp>
#include <isola/Sky.hpp>
#include <isola/Terrain.hpp>
#include <isola/Version.hpp>
