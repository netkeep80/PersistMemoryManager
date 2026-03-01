/**
 * @file scenario_manager.cpp
 * @brief Implementation of ScenarioManager.
 */

#include "scenario_manager.h"

#include "imgui.h"

#include <chrono>

namespace demo
{

// Forward declaration of factory function defined in scenarios.cpp
std::vector<std::unique_ptr<Scenario>> create_all_scenarios();

// ─── Constructor / Destructor ─────────────────────────────────────────────────

ScenarioManager::ScenarioManager() : scenarios_( create_all_scenarios() )
{

    for ( auto& sc : scenarios_ )
    {
        auto state  = std::make_unique<ScenarioState>();
        state->name = sc->name();

        // Default params per scenario (indices match create_all_scenarios order)
        // 0: LinearFill
        // 1: RandomStress
        // 2: FragmentationDemo
        // 3: LargeBlocks
        // 4: TinyBlocks
        // 5: MixedSizes
        // 6: PersistenceCycle

        states_.push_back( std::move( state ) );
    }

    // Override default params for specific scenarios
    if ( states_.size() > 0 )
    {
        states_[0]->params.min_block_size = 256;
        states_[0]->params.max_block_size = 256;
        states_[0]->params.alloc_freq     = 500.0f;
        states_[0]->params.dealloc_freq   = 0.0f;
    }
    if ( states_.size() > 1 )
    {
        states_[1]->params.min_block_size  = 64;
        states_[1]->params.max_block_size  = 4096;
        states_[1]->params.alloc_freq      = 2000.0f;
        states_[1]->params.dealloc_freq    = 1800.0f;
        states_[1]->params.max_live_blocks = 100;
    }
    if ( states_.size() > 2 )
    {
        states_[2]->params.min_block_size = 16;
        states_[2]->params.max_block_size = 16384;
        states_[2]->params.alloc_freq     = 300.0f;
        states_[2]->params.dealloc_freq   = 250.0f;
    }
    if ( states_.size() > 3 )
    {
        states_[3]->params.min_block_size = 65536;
        states_[3]->params.max_block_size = 262144;
        states_[3]->params.alloc_freq     = 20.0f;
        states_[3]->params.dealloc_freq   = 18.0f;
    }
    if ( states_.size() > 4 )
    {
        states_[4]->params.min_block_size  = 8;
        states_[4]->params.max_block_size  = 32;
        states_[4]->params.alloc_freq      = 10000.0f;
        states_[4]->params.dealloc_freq    = 9500.0f;
        states_[4]->params.max_live_blocks = 200;
    }
    if ( states_.size() > 5 )
    {
        states_[5]->params.min_block_size  = 32;
        states_[5]->params.max_block_size  = 32768;
        states_[5]->params.alloc_freq      = 1000.0f;
        states_[5]->params.dealloc_freq    = 950.0f;
        states_[5]->params.max_live_blocks = 100;
    }
    if ( states_.size() > 6 )
    {
        states_[6]->params.min_block_size = 128;
        states_[6]->params.max_block_size = 1024;
        states_[6]->params.alloc_freq     = 0.2f; // 1 cycle per 5 s
        states_[6]->params.dealloc_freq   = 0.0f;
    }
}

ScenarioManager::~ScenarioManager()
{
    stop_all();
    join_all();
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void ScenarioManager::start( std::size_t index )
{
    if ( index >= scenarios_.size() )
        return;

    auto& state = *states_[index];
    if ( state.running.load() )
        return;

    state.stop_flag.store( false );
    state.running.store( true );

    ScenarioParams params_copy = state.params;

    state.thread = std::thread(
        [this, index, params_copy]() mutable
        {
            scenarios_[index]->run( states_[index]->stop_flag, states_[index]->total_ops, params_copy );
            states_[index]->running.store( false );
        } );
}

void ScenarioManager::stop( std::size_t index )
{
    if ( index >= states_.size() )
        return;

    auto& state = *states_[index];
    state.stop_flag.store( true );

    if ( state.thread.joinable() )
        state.thread.join();

    state.running.store( false );
}

void ScenarioManager::start_all()
{
    for ( std::size_t i = 0; i < scenarios_.size(); ++i )
        start( i );
}

void ScenarioManager::stop_all()
{
    for ( auto& st : states_ )
        st->stop_flag.store( true );
}

void ScenarioManager::join_all()
{
    for ( auto& st : states_ )
    {
        if ( st->thread.joinable() )
            st->thread.join();
        st->running.store( false );
    }
}

std::size_t ScenarioManager::count() const
{
    return scenarios_.size();
}

float ScenarioManager::total_ops_per_sec() const
{
    // Simple snapshot: caller is responsible for sampling periodically.
    return 0.0f;
}

// ─── UI ───────────────────────────────────────────────────────────────────────

void ScenarioManager::render()
{
    ImGui::Begin( "Scenarios" );

    if ( ImGui::Button( "Start All" ) )
        start_all();

    ImGui::SameLine();

    if ( ImGui::Button( "Stop All" ) )
        stop_all();

    ImGui::Separator();

    ImGui::Columns( 5, "scenario_cols", true );
    ImGui::Text( "Controls" );
    ImGui::NextColumn();
    ImGui::Text( "Scenario" );
    ImGui::NextColumn();
    ImGui::Text( "Status" );
    ImGui::NextColumn();
    ImGui::Text( "Ops" );
    ImGui::NextColumn();
    ImGui::Text( "Params" );
    ImGui::NextColumn();
    ImGui::Separator();
    ImGui::Columns( 1 );

    for ( std::size_t i = 0; i < scenarios_.size(); ++i )
        render_scenario_row( i );

    ImGui::End();
}

void ScenarioManager::render_scenario_row( std::size_t i )
{
    auto& state   = *states_[i];
    bool  running = state.running.load();

    ImGui::PushID( static_cast<int>( i ) );

    // Start button
    if ( running )
        ImGui::BeginDisabled();
    if ( ImGui::SmallButton( ">" ) )
        start( i );
    if ( running )
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Stop button
    if ( !running )
        ImGui::BeginDisabled();
    if ( ImGui::SmallButton( "[]" ) )
        stop( i );
    if ( !running )
        ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text( "%s", state.name.c_str() );

    ImGui::SameLine( 0, 20 );

    if ( running )
        ImGui::TextColored( ImVec4( 0.2f, 0.8f, 0.2f, 1.0f ), "RUNNING" );
    else
        ImGui::TextColored( ImVec4( 0.5f, 0.5f, 0.5f, 1.0f ), "STOPPED" );

    ImGui::SameLine( 0, 20 );
    ImGui::Text( "ops: %llu", static_cast<unsigned long long>( state.total_ops.load() ) );

    ImGui::SameLine( 0, 20 );
    if ( ImGui::SmallButton( state.show_params ? "[-]" : "[+]" ) )
        state.show_params = !state.show_params;

    if ( state.show_params )
    {
        ImGui::Indent( 20.0f );
        int min_sz   = static_cast<int>( state.params.min_block_size );
        int max_sz   = static_cast<int>( state.params.max_block_size );
        int max_live = state.params.max_live_blocks;

        if ( ImGui::InputInt( "Min size", &min_sz ) && min_sz > 0 )
            state.params.min_block_size = static_cast<std::size_t>( min_sz );
        if ( ImGui::InputInt( "Max size", &max_sz ) && max_sz > 0 )
            state.params.max_block_size = static_cast<std::size_t>( max_sz );
        ImGui::SliderFloat( "Alloc freq", &state.params.alloc_freq, 1.0f, 20000.0f );
        ImGui::SliderFloat( "Dealloc freq", &state.params.dealloc_freq, 0.0f, 20000.0f );
        if ( ImGui::InputInt( "Max live", &max_live ) && max_live > 0 )
            state.params.max_live_blocks = max_live;

        ImGui::Unindent( 20.0f );
    }

    ImGui::PopID();
}

} // namespace demo
