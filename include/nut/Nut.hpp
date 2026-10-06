#pragma once

/// Nut - a small OpenGL game engine: procedural islands with oceans, forests and villages,
/// the Sponza palace, and a first-person player.
///
/// Include this header for the whole public API:
///   nut::Engine          the engine (window, world, player, main loop)
///   nut::EngineConfig    start-up settings
///   nut::Map             the available worlds
///   nut::TerrainParams, nut::FoliageParams, nut::GraphicsSettings, nut::SkySettings, nut::SunSettings
///   nut::Key             keyboard keys for input callbacks
///   nut::Error           the exception type

#include <nut/Config.hpp>
#include <nut/Engine.hpp>
#include <nut/Error.hpp>
#include <nut/Foliage.hpp>
#include <nut/Graphics.hpp>
#include <nut/Input.hpp>
#include <nut/Map.hpp>
#include <nut/Sky.hpp>
#include <nut/Terrain.hpp>
#include <nut/Version.hpp>
