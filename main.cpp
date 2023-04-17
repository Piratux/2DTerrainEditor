/**
 * Controls:
 * - WASD: move player (red ball)
 * - Left mouse button: Apply selected tool onto raycasted area (if it hits the "terrain").
 * - Left mouse button + CTRL: Apply selected tool onto raycasted area just once on click.
 * - Right mouse button: Apply selected tool onto raycasted area (if it hits the "terrain").
 * - Right mouse button + CTRL: Apply selected tool onto raycasted area just once on click.
 * - Middle mouse button (aka scroll button): Pan around
 * - Middle mouse button + CTRL: Draw white ball (terrain).
 * - Scrolling: Zoom in/out
 * - Scrolling + CTRL: Increase/decrease size of brush.
 * - Enter: Clear away canvas.
 * - Space: Show/hide player/mouse circles.
 * - Keys 1-5: Select terraform tool.
 * - Escape: Reset transformed view (reset zoom and panned position)
 * - Shift: Increase player speed 2.5x times.
*/


#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#define OLC_PGEX_TRANSFORMEDVIEW
#include "olcPGEX_TransformedView.h"

#define _USE_MATH_DEFINES
#include <math.h>

#include <queue>

template <typename T>
class BlockBuffer{
	std::vector<T> buffer;
	int x_offset;
	int y_offset;
	int x_size;
	int y_size;

public:
	// from and to inclusive
	BlockBuffer(olc::vi2d from, olc::vi2d to){
		x_offset = from.x;
		y_offset = from.y;
		x_size = to.x - from.x + 1;
		y_size = to.y - from.y + 1;
		buffer.resize(x_size * y_size);
		std::fill(buffer.begin(), buffer.end(), T()));
	}

	int get_index(int x, int y){
		return (y - y_offset) * x_size + (x - x_offset);
	}
	void set(int x, int y, T value){
		int index = get_index(x, y);
		buffer[index] = value;
	}
	T get(int x, int y){
		return buffer[get_index(x, y)];
	}
};

class Example : public olc::PixelGameEngine
{
public:
	enum class EditMode {
		CircleFull,
		CircleFractional,
		GaussFractional,
		AdjustTerrain_BlendBallFull,
		AdjustTerrain_BlendBallFractional
	};

	EditMode mode = EditMode::AdjustTerrain_BlendBallFractional;

	olc::TileTransformedView tv;
	
	olc::vi2d map_size = { 512, 512 };
	olc::vf2d player_pos = map_size/2;

	olc::vf2d mouse_pos;

	olc::Sprite map{map_size.x, map_size.y};

	int blend_range = 15;

	int brush_size = 16;
	float brush_size_f = brush_size;
	float brush_size_min = 4.0f;
	float brush_size_max = 200.0f;
	float brush_size_multiplier = 1.1f;

	float speed = 100.0f;
	float raycast_max_distance = 500.0f;

	float draw_speed = (2.0f / 60.0f);
	float accumulate_delta = 0.0f;
	bool can_edit_terrain = true;

	bool draw_edit_tools = true;

	float terraform_angle = 50.0f;
	float terraform_raycast_step = 0.5f;

	Example()
	{
		sAppName = "Editor";
	}

public:
	bool OnUserCreate() override
	{
		ResetMap();

		tv = olc::TileTransformedView({ ScreenWidth(), ScreenHeight() }, { 1, 1 });

		return true;
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		if(!GetKey(olc::Key::CTRL).bHeld){
			tv.HandlePanAndZoom();
		}
		
		Clear(olc::BLACK);

		if (accumulate_delta > draw_speed) {
			can_edit_terrain = true;
		}
		else {
			accumulate_delta += fElapsedTime;
		}

		// Form ray cast from player into scene
		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();

		mouse_pos = { float(GetMouseX()), float(GetMouseY()) };
		mouse_pos = tv.ScreenToWorld(mouse_pos);

		if(GetKey(olc::Key::CTRL).bHeld){
			if (GetMouseWheel() > 0) {
				brush_size_f *= brush_size_multiplier;
				brush_size_f = std::clamp(brush_size_f, brush_size_min, brush_size_max);
				brush_size = std::floor(brush_size_f);
			}
			else if (GetMouseWheel() < 0){
				brush_size_f /= brush_size_multiplier;
				brush_size_f = std::clamp(brush_size_f, brush_size_min, brush_size_max);
				brush_size = std::floor(brush_size_f);
			}

			if (GetMouse(2).bHeld) {
				PaintMouseLocation(mouse_pos);
			}
		}

		if (GetKey(olc::Key::ENTER).bPressed) {
			ResetMap();
		}

		if (GetKey(olc::Key::ESCAPE).bPressed) {
			tv.SetWorldScale({1.0f, 1.0f});
			tv.SetWorldOffset({0.0f, 0.0f});
		}

		if (GetKey(olc::Key::SPACE).bPressed) {
			draw_edit_tools = !draw_edit_tools;
		}		

		float player_speed = speed;
		if (GetKey(olc::Key::SHIFT).bHeld) {
			player_speed *= 2.5f;
		}

		if (GetKey(olc::Key::W).bHeld) player_pos.y -= player_speed * fElapsedTime;
		if (GetKey(olc::Key::S).bHeld) player_pos.y += player_speed * fElapsedTime;
		if (GetKey(olc::Key::A).bHeld) player_pos.x -= player_speed * fElapsedTime;
		if (GetKey(olc::Key::D).bHeld) player_pos.x += player_speed * fElapsedTime;

		if (GetKey(olc::Key::K1).bPressed) {
			mode = EditMode::CircleFull;
		}
		else if (GetKey(olc::Key::K2).bPressed) {
			mode = EditMode::CircleFractional;
		}
		else if (GetKey(olc::Key::K3).bPressed) {
			mode = EditMode::GaussFractional;
		}
		else if (GetKey(olc::Key::K4).bPressed) {
			mode = EditMode::AdjustTerrain_BlendBallFull;
		}
		else if (GetKey(olc::Key::K5).bPressed) {
			mode = EditMode::AdjustTerrain_BlendBallFractional;
		}

		olc::vi2d intersection_result;
		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());
		bool raycast_hit = RaycastPixelTarget(ray_start_pos, ray_dir, max_distance, intersection_result);

		if (raycast_hit) {
			// Right mouse button
			if (GetKey(olc::Key::CTRL).bHeld && GetMouse(1).bPressed || !GetKey(olc::Key::CTRL).bHeld && GetMouse(1).bHeld) {
				if (can_edit_terrain) {
					can_edit_terrain = false;
					accumulate_delta = 0.0f;

					switch (mode) {
					case EditMode::CircleFull:
						DestructTerrain_CircleFull(intersection_result, ray_dir);
						break;
					case EditMode::CircleFractional:
						DestructTerrain_CircleFractional(intersection_result, ray_dir);
						break;
					case EditMode::GaussFractional:
						DestructTerrain_GaussFractional(intersection_result, ray_dir);
						break;
					case EditMode::AdjustTerrain_BlendBallFull:
						AdjustTerrain_BlendBallFractional(intersection_result, ray_dir);
						break;
					case EditMode::AdjustTerrain_BlendBallFractional:
						AdjustTerrain_BlendBallFractionalFast2();
						break;
					default:
						std::cout << "Error\n";
					}
				}
			}

			// Left mouse button
			else if (GetKey(olc::Key::CTRL).bHeld && GetMouse(0).bPressed || !GetKey(olc::Key::CTRL).bHeld && GetMouse(0).bHeld) {
				if (can_edit_terrain) {
					can_edit_terrain = false;
					accumulate_delta = 0.0f;

					switch (mode) {
					case EditMode::CircleFull:
						DestructTerrain_CircleFull(intersection_result, ray_dir);
						break;
					case EditMode::CircleFractional:
						DestructTerrain_CircleFractional(intersection_result, ray_dir);
						break;
					case EditMode::GaussFractional:
						RestoreTerrain_GaussFractional(intersection_result, ray_dir);
						break;
					case EditMode::AdjustTerrain_BlendBallFull:
						AdjustTerrain_BlendBallFractional(intersection_result, ray_dir);
						break;
					case EditMode::AdjustTerrain_BlendBallFractional:
						AdjustTerrain_BlendBallFractional(intersection_result, ray_dir);
						break;
					default:
						std::cout << "Error\n";
					}
				}
			}
		}

		tv.DrawSprite({ 0,0 }, &map);

		if(draw_edit_tools){
			if (raycast_hit) {
				tv.DrawCircle(intersection_result, brush_size, olc::Pixel(0x3e, 0x95, 0xef));
			}

			tv.DrawLine(player_pos, mouse_pos, olc::Pixel(0xd38e28ff), 0xF0F0F0F0);

			// Draw Player
			tv.FillCircle(player_pos, 8, olc::RED);

			// Draw Mouse
			if (GetMouse(0).bHeld || GetMouse(1).bHeld) {
				tv.FillCircle(mouse_pos, brush_size, olc::GREEN);
			}
			else {
				tv.FillCircle(mouse_pos, brush_size, olc::DARK_GREEN);
			}
		}
		
		return true;
	}

	bool RaycastPixel(olc::vi2d ray_start_pos, olc::vf2d ray_dir, float max_distance, olc::vi2d& intersection_result) {
		// DDA Algorithm ==============================================
		// https://lodev.org/cgtutor/raycasting.html


		// Lodev.org also explains this additional optimistaion (but it's beyond scope of video)
		// olc::vf2d vRayUnitStepSize = { abs(1.0f / ray_dir.x), abs(1.0f / ray_dir.y) };

		olc::vf2d vRayUnitStepSize = { sqrt(1 + (ray_dir.y / ray_dir.x) * (ray_dir.y / ray_dir.x)), sqrt(1 + (ray_dir.x / ray_dir.y) * (ray_dir.x / ray_dir.y)) };
		olc::vi2d vMapCheck = ray_start_pos;
		olc::vf2d vRayLength1D;
		olc::vi2d vStep;

		// Establish Starting Conditions
		if (ray_dir.x < 0)
		{
			vStep.x = -1;
			vRayLength1D.x = (ray_start_pos.x - float(vMapCheck.x)) * vRayUnitStepSize.x;
		}
		else
		{
			vStep.x = 1;
			vRayLength1D.x = (float(vMapCheck.x + 1) - ray_start_pos.x) * vRayUnitStepSize.x;
		}

		if (ray_dir.y < 0)
		{
			vStep.y = -1;
			vRayLength1D.y = (ray_start_pos.y - float(vMapCheck.y)) * vRayUnitStepSize.y;
		}
		else
		{
			vStep.y = 1;
			vRayLength1D.y = (float(vMapCheck.y + 1) - ray_start_pos.y) * vRayUnitStepSize.y;
		}

		// Perform "Walk" until collision or range check
		bool bTileFound = false;
		float fDistance = 0.0f;
		while (!bTileFound && fDistance < max_distance)
		{
			// Walk along shortest path
			if (vRayLength1D.x < vRayLength1D.y)
			{
				vMapCheck.x += vStep.x;
				fDistance = vRayLength1D.x;
				vRayLength1D.x += vRayUnitStepSize.x;
			}
			else
			{
				vMapCheck.y += vStep.y;
				fDistance = vRayLength1D.y;
				vRayLength1D.y += vRayUnitStepSize.y;
			}

			// Test tile at new test point
			if (vMapCheck.x >= 0 && vMapCheck.x < map_size.x && vMapCheck.y >= 0 && vMapCheck.y < map_size.y)
			{
				if (MapLocationIsEmpty(vMapCheck) == false)
				{
					bTileFound = true;
				}
			}
		}

		intersection_result = vMapCheck;

		// Calculate intersection location
		/*if (bTileFound)
		{

			intersection_result = ray_start_pos + ray_dir * fDistance;
		}*/

		return bTileFound;
	}

	bool RaycastPrePixel(olc::vi2d ray_start_pos, olc::vf2d ray_dir, float max_distance, olc::vi2d& intersection_result) {
		// DDA Algorithm ==============================================
		// https://lodev.org/cgtutor/raycasting.html


		// Lodev.org also explains this additional optimistaion (but it's beyond scope of video)
		// olc::vf2d vRayUnitStepSize = { abs(1.0f / ray_dir.x), abs(1.0f / ray_dir.y) };

		olc::vf2d vRayUnitStepSize = { sqrt(1 + (ray_dir.y / ray_dir.x) * (ray_dir.y / ray_dir.x)), sqrt(1 + (ray_dir.x / ray_dir.y) * (ray_dir.x / ray_dir.y)) };
		olc::vi2d vMapCheck = ray_start_pos;
		olc::vf2d vRayLength1D;
		olc::vi2d vStep;

		// Establish Starting Conditions
		if (ray_dir.x < 0)
		{
			vStep.x = -1;
			vRayLength1D.x = (ray_start_pos.x - float(vMapCheck.x)) * vRayUnitStepSize.x;
		}
		else
		{
			vStep.x = 1;
			vRayLength1D.x = (float(vMapCheck.x + 1) - ray_start_pos.x) * vRayUnitStepSize.x;
		}

		if (ray_dir.y < 0)
		{
			vStep.y = -1;
			vRayLength1D.y = (ray_start_pos.y - float(vMapCheck.y)) * vRayUnitStepSize.y;
		}
		else
		{
			vStep.y = 1;
			vRayLength1D.y = (float(vMapCheck.y + 1) - ray_start_pos.y) * vRayUnitStepSize.y;
		}

		// Perform "Walk" until collision or range check
		bool bTileFound = false;
		float fDistance = 0.0f;
		olc::vi2d previous_tile = vMapCheck;
		while (!bTileFound && fDistance < max_distance)
		{
			previous_tile = vMapCheck;

			// Walk along shortest path
			if (vRayLength1D.x < vRayLength1D.y)
			{
				vMapCheck.x += vStep.x;
				fDistance = vRayLength1D.x;
				vRayLength1D.x += vRayUnitStepSize.x;
			}
			else
			{
				vMapCheck.y += vStep.y;
				fDistance = vRayLength1D.y;
				vRayLength1D.y += vRayUnitStepSize.y;
			}

			// Test tile at new test point
			if (vMapCheck.x >= 0 && vMapCheck.x < map_size.x && vMapCheck.y >= 0 && vMapCheck.y < map_size.y)
			{
				if (MapLocationIsEmpty(vMapCheck) == false)
				{
					bTileFound = true;
				}
			}
		}

		if (MapLocationIsFull(vMapCheck)) {
			intersection_result = previous_tile;
		}
		else {
			intersection_result = vMapCheck;
		}

		// Calculate intersection location
		/*if (bTileFound)
		{

			intersection_result = ray_start_pos + ray_dir * fDistance;
		}*/

		return bTileFound;
	}

	bool RaycastPixelTarget(olc::vi2d ray_start_pos, olc::vf2d ray_dir, float max_distance, olc::vi2d& intersection_result) {
		// DDA Algorithm ==============================================
		// https://lodev.org/cgtutor/raycasting.html


		// Lodev.org also explains this additional optimistaion (but it's beyond scope of video)
		// olc::vf2d vRayUnitStepSize = { abs(1.0f / ray_dir.x), abs(1.0f / ray_dir.y) };

		olc::vf2d vRayUnitStepSize = { sqrt(1 + (ray_dir.y / ray_dir.x) * (ray_dir.y / ray_dir.x)), sqrt(1 + (ray_dir.x / ray_dir.y) * (ray_dir.x / ray_dir.y)) };
		olc::vi2d vMapCheck = ray_start_pos;
		olc::vf2d vRayLength1D;
		olc::vi2d vStep;

		// Establish Starting Conditions
		if (ray_dir.x < 0)
		{
			vStep.x = -1;
			vRayLength1D.x = (ray_start_pos.x - float(vMapCheck.x)) * vRayUnitStepSize.x;
		}
		else
		{
			vStep.x = 1;
			vRayLength1D.x = (float(vMapCheck.x + 1) - ray_start_pos.x) * vRayUnitStepSize.x;
		}

		if (ray_dir.y < 0)
		{
			vStep.y = -1;
			vRayLength1D.y = (ray_start_pos.y - float(vMapCheck.y)) * vRayUnitStepSize.y;
		}
		else
		{
			vStep.y = 1;
			vRayLength1D.y = (float(vMapCheck.y + 1) - ray_start_pos.y) * vRayUnitStepSize.y;
		}

		// Perform "Walk" until collision or range check
		bool bTileFound = false;
		float fDistance = 0.0f;
		while (!bTileFound && fDistance < max_distance)
		{
			// Walk along shortest path
			if (vRayLength1D.x < vRayLength1D.y)
			{
				vMapCheck.x += vStep.x;
				fDistance = vRayLength1D.x;
				vRayLength1D.x += vRayUnitStepSize.x;
			}
			else
			{
				vMapCheck.y += vStep.y;
				fDistance = vRayLength1D.y;
				vRayLength1D.y += vRayUnitStepSize.y;
			}

			// Test tile at new test point
			if (vMapCheck.x >= 0 && vMapCheck.x < map_size.x && vMapCheck.y >= 0 && vMapCheck.y < map_size.y)
			{
				if(GetColourValue(vMapCheck) >= 0.5f){
					bTileFound = true;
				}
			}
		}

		intersection_result = vMapCheck;

		return bTileFound;
	}

	void PaintMouseLocation(olc::vi2d vCell) {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		int32_t radius = 32;
		olc::vf2d size = { float(radius), float(radius) };
		FillCircle(vCell, radius);
		
		SetDrawTarget(previous_target);
	}

	// void AdjustTerrain_BlendBallFull(olc::vf2d vCell, olc::vf2d direction){
    //     double brush_size_squared = brush_size * brush_size;
    //     // all changes are initially performed into a buffer to prevent the
    //     // results bleeding into each other

	// 	BlockBuffer<int> buffer{{-brush_size, -brush_size}, {brush_size, brush_size}};

	// 	olc::vf2d ray_start_pos = player_pos;
	// 	olc::vf2d ray_dir = (mouse_pos - player_pos).norm();
	// 	olc::vi2d intersection_pos;

	// 	float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());

	// 	bool raycast_hit = RaycastPixel(ray_start_pos, ray_dir, max_distance, intersection_pos);
	// 	if(!raycast_hit){
	// 		return;
	// 	}

    //     int tx = intersection_pos.x;
    //     int ty = intersection_pos.y;

	// 	std::map<int, int> frequency;

    //     for (int x = -brush_size; x <= brush_size; x++) {
    //         int x0 = x + tx;
    //         for (int y = -brush_size; y <= brush_size; y++) {
    //             int y0 = y + ty;

	// 			int currentState;
	// 			if(!MapLocationIsEmpty(olc::vi2d{x0, y0})){
	// 				currentState = 1;
	// 			}
	// 			else{
	// 				currentState = 0;
	// 			}

	// 			if (x * x + y * y >= brush_size_squared) {
	// 				buffer.set(x, y, currentState);
	// 				continue;
	// 			}
	// 			int highest = 1;
	// 			int highestState = currentState;
	// 			frequency.clear();
	// 			bool tie = false;
	// 			for (int ox = -blend_range; ox <= blend_range; ox++) {
	// 				for (int oy = -blend_range; oy <= blend_range; oy++) {
	// 					int state;
	// 					if(!MapLocationIsEmpty({x0 + ox, y0 + oy})){
	// 						state = 1;
	// 					}
	// 					else{
	// 						state = 0;
	// 					}
	// 					int count = frequency[state];
	// 					if (count == 0) {
	// 						count = 1;
	// 					} else {
	// 						count++;
	// 					}
	// 					if (count > highest) {
	// 						highest = count;
	// 						highestState = state;
	// 						tie = false;
	// 					} else if (count == highest) {
	// 						tie = true;
	// 					}
	// 					frequency[state] = count;
	// 				}
	// 			}
	// 			if (!tie && currentState != highestState) {
	// 				buffer.set(x, y, highestState);
	// 			}
    //         }
    //     }

    //     // apply the buffer to the world
    //     for (int x = -brush_size; x <= brush_size; x++) {
    //         int x0 = x + tx;
    //         for (int y = -brush_size; y <= brush_size; y++) {
    //             int y0 = y + ty;
	// 			if(buffer.contains(x, y)){
	// 				SetColourValue({x0, y0}, (float)buffer.get(x, y));
	// 			}
    //         }
    //     }
	// }

	void AdjustTerrain_BlendBallFractional(olc::vf2d vCell, olc::vf2d direction){
		double brush_size_squared = brush_size * brush_size;

		// all changes are initially performed into a buffer to prevent the
		// results bleeding into each other
		BlockBuffer<float> buffer{{-brush_size, -brush_size}, {brush_size, brush_size}};

		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();
		olc::vi2d intersection_pos;

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());

		bool raycast_hit = RaycastPixel(ray_start_pos, ray_dir, max_distance, intersection_pos);
		if(!raycast_hit){
			return;
		}

		int tx = intersection_pos.x;
		int ty = intersection_pos.y;

		for (int x = -brush_size; x <= brush_size; x++) {
			int x0 = x + tx;
			for (int y = -brush_size; y <= brush_size; y++) {
				int y0 = y + ty;
				double distance = x * x + y * y;
				if (distance >= brush_size_squared) {
					buffer.set(x, y, GetColourValue({x0, y0}));
					continue;
				}
				float max_sum_weighted = 0.0f;
				float colour_sum_weighted = 0.0f;

				for (int ox = -blend_range; ox <= blend_range; ox++) {
					for (int oy = -blend_range; oy <= blend_range; oy++) {
						olc::vi2d pos{x0 + ox, y0 + oy};

						max_sum_weighted += 1;
						colour_sum_weighted += GetColourValue(pos);
					}
				}

				float average = colour_sum_weighted / max_sum_weighted;
				// NOTE: When using for real thing, check if value needs to be remapped to avoid floating errors
				average = EaseInOutCubic(average);

				float curr_voxel_value = GetColourValue({x0, y0});
				
				float distance_normalised = distance / brush_size_squared;
				distance_normalised = std::max(distance_normalised * 2.0f - 1.0f, 0.0f);
				float new_voxel_value = curr_voxel_value * distance_normalised + average * (1 - distance_normalised);

				buffer.set(x, y, new_voxel_value);
			}
		}

		// apply the changes
		for (int x = -brush_size; x <= brush_size; x++) {
			int x0 = x + tx;
			for (int y = -brush_size; y <= brush_size; y++) {
				int y0 = y + ty;
				SetColourValue({x0, y0}, buffer.get(x, y));
			}
		}
	}

	void AdjustTerrain_BlendBallFractionalFast(){
		std::vector<float> sums;
	
		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();
		olc::vi2d intersection_pos;

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());

		bool raycast_hit = RaycastPixel(ray_start_pos, ray_dir, max_distance, intersection_pos);
		if(!raycast_hit){
			return;
		}
	
		int tx = intersection_pos.x;
		int ty = intersection_pos.y;
	
		int blend_range = 5;
	
		// for distance normalization
		double brush_size_squared = brush_size * brush_size;
	
		double brush_region_half = brush_size + blend_range;
		double brush_region_size = 2 * brush_region_half + 1;
		double brush_region_squared = brush_region_size * brush_region_size;
	
		int x, y, i, dx, dy, x0, y0, xmin, xmax, ymin, ymax;
		double distance, area;
		float a, b, c, d, sum, average, distance_normalised, cur_voxel_value, new_voxel_value;
	
		// create the summed-area table for the selected region
		// https://en.wikipedia.org/wiki/Summed-area_table
		sums.resize(brush_region_squared, 0);
		for (y = 0, i = 0; y < brush_region_size; ++y) {
			dy = y - brush_region_half;
			y0 = ty + dy;
			for (x = 0; x < brush_region_size; ++x) {
				dx = x - brush_region_half;
				x0 = tx + dx;
				/* sample the current voxel value, and add the sums to the left and top of it
				   (minus the overlap because otherwise it is counted twice) */
				sum = GetColourValue({x0, y0});
				if (x > 0) sum += sums[i-1];
				if (y > 0) sum += sums[i-brush_region_size];
				if (x > 0 && y > 0) sum -= sums[i-(brush_region_size+1)];
				sums[i++] = sum;
			}
		}
	
		// apply brush to region
		for (y = 0, i = 0; y < brush_region_size; ++y) {
			dy = y - brush_region_half;
			y0 = ty + dy;
			ymin = std::max(y - blend_range, 0) - 1;
			ymax = std::min(y + blend_range, (int)(brush_region_size - 1));
		
			for (x = 0; x < brush_region_size; ++x) {
				dx = x - brush_region_half;
				x0 = tx + dx;
				xmin = std::max(x - blend_range, 0) - 1;
				xmax = std::min(x + blend_range, (int)(brush_region_size - 1));
			
				area = (xmax - xmin) * (ymax - ymin);
			
				/* using the summed-area table method, we only need to 
				   sample 4 points to get the sum of the brush region */
				a = xmin < 0 || ymin < 0 ? 0 : sums[ymin*brush_region_size+xmin];
				b = ymin < 0 ? 0 : sums[ymin*brush_region_size+xmax];
				c = xmin < 0 ? 0 : sums[ymax*brush_region_size+xmin];
				d = sums[ymax*brush_region_size+xmax];
				sum = (d + a) - (b + c);
			
				average = sum / area;
				average = EaseInOutCubic(average);
			
				distance = dx * dx + dy * dy;
				distance_normalised = 1.0 - (distance / brush_size_squared);
				// distance_normalised = (distance / brush_size_squared);
				distance_normalised = std::max(distance_normalised * 2.0 - 1.0, 0.0);
			
				olc::vi2d pos{x0,y0};
			
				cur_voxel_value = GetColourValue(pos);
				new_voxel_value = Lerp(cur_voxel_value, average, distance_normalised);
				// new_voxel_value = Lerp(average, cur_voxel_value, distance_normalised);
			
				SetColourValue(pos, new_voxel_value);
			}
		}
	}

	// NOTE: this pre-average method requires less and less iterations after each average
	// TODO: analyse if code will work well with blend_range > brush_size
	void AdjustTerrain_BlendBallFractionalFast2(){
		int brush_size_squared = brush_size * brush_size;

		// TODO: think of better variable name for this
		int extended_brush_size = brush_size + blend_range;

		// all changes are initially performed into a buffer to prevent the
		// results bleeding into each other
		BlockBuffer<float> buffer{{-extended_brush_size, -extended_brush_size}, {extended_brush_size, extended_brush_size}};

		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();
		olc::vi2d intersection_pos;

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());

		bool raycast_hit = RaycastPixel(ray_start_pos, ray_dir, max_distance, intersection_pos);
		if(!raycast_hit){
			return;
		}

		int brush_pos_x = intersection_pos.x;
		int brush_pos_y = intersection_pos.y;

		float total_blend_elements = 2 * blend_range + 1;

		// x blur
		for (int y = -extended_brush_size; y <= extended_brush_size; y++) {
			int y0 = y + brush_pos_y;
			float colour_sum = 0.0f;
			std::queue<float> buffered_values;

			for (int x = -brush_size; x <= brush_size; x++){
				int x0 = x + brush_pos_x;

				if(x == -brush_size){
					for (int ox = -blend_range; ox <= blend_range; ox++){
						float value = GetColourValue({x0 + ox, y0});
						buffered_values.push(value);
						colour_sum += value;
					}
				}
				else{
					float value = GetColourValue({x0 + blend_range, y0});
					buffered_values.push(value);
					colour_sum += value;

					colour_sum -= buffered_values.front();
					buffered_values.pop();
				}

				float average = colour_sum / total_blend_elements;
				buffer.set(x, y, average);
			}
		}

		// y blur
		for (int x = -brush_size; x <= brush_size; x++) {
			float colour_sum = 0.0f;
			std::queue<float> buffered_values;

			for (int y = -brush_size; y <= brush_size; y++){
				if(y == -brush_size){
					for (int oy = -blend_range; oy <= blend_range; oy++){
						float value = buffer.get(x, y + oy);
						buffered_values.push(value);
						colour_sum += value;
					}
				}
				else{
					float value = buffer.get(x, y + blend_range);
					buffered_values.push(value);
					colour_sum += value;

					colour_sum -= buffered_values.front();
					buffered_values.pop();
				}

				float average = colour_sum / total_blend_elements;
				buffer.set(x, y, average);
			}
		}

		// processing and pasting result
		for (int x = -brush_size; x <= brush_size; x++) {
			int x0 = x + brush_pos_x;
			for (int y = -brush_size; y <= brush_size; y++){
				int y0 = y + brush_pos_y;

				int distance = x * x + y * y;
				if (distance >= brush_size_squared) {
					continue;
				}

				float average = buffer.get(x, y);

				average = EaseInOutCubic(average);

				float curr_voxel_value = GetColourValue({x0, y0});
				
				float distance_normalised = (float)distance / (float)brush_size_squared;
				distance_normalised = std::max(distance_normalised * 2.0f - 1.0f, 0.0f);
				float new_voxel_value = Lerp(average, curr_voxel_value, distance_normalised);

				SetColourValue({x0, y0}, new_voxel_value);
			}
		}
	}

	// 230 -> 35
	/*void AdjustTerrain_BlendBallFractionalFast(olc::vf2d vCell, olc::vf2d direction){
		double brush_size_squared = brush_size * brush_size;

		// all changes are initially performed into a buffer to prevent the
		// results bleeding into each other
		BlockBuffer<float> buffer{{-brush_size, -brush_size}, {brush_size, brush_size}};

		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();
		olc::vi2d intersection_pos;

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());

		bool raycast_hit = RaycastPixel(ray_start_pos, ray_dir, max_distance, intersection_pos);
		if(!raycast_hit){
			return;
		}

		int tx = intersection_pos.x;
		int ty = intersection_pos.y;

		// Optimised version doesn't fully average corners, but we calculate if we need to increase brush size to accomdate this.
		int extra_iterations = 0;
		if(blend_range + M_PI_4 >= brush_size){
			extra_iterations = std::ceil(blend_range + M_PI_4 - brush_size);
		}

		int adjusted_brush_size = brush_size + extra_iterations;
		for (int x = -adjusted_brush_size; x <= adjusted_brush_size; x++) {
			int x0 = x + tx;
			for (int y = -adjusted_brush_size; y <= adjusted_brush_size; y++) {
				int y0 = y + ty;

				float max_sum_weighted = 0.0f;
				float colour_sum_weighted = 0.0f;

				for (int ox = -blend_range; ox <= blend_range; ox++) {
					for (int oy = -blend_range; oy <= blend_range; oy++) {
						olc::vi2d pos{x0 + ox, y0 + oy};

						max_sum_weighted += 1;
						colour_sum_weighted += GetColourValue(pos);
					}
				}

				float average = colour_sum_weighted / max_sum_weighted;
				// NOTE: When using for real thing, check if value needs to be remapped to avoid floating errors
				average = EaseInOutCubic(average);

				float curr_voxel_value = GetColourValue({x0, y0});
				
				float distance_normalised = distance / brush_size_squared;
				distance_normalised = std::max(distance_normalised * 2.0f - 1.0f, 0.0f);
				float new_voxel_value = curr_voxel_value * distance_normalised + average * (1 - distance_normalised);

				buffer.set(x, y, new_voxel_value);
			}
		}

		for (int x = -brush_size; x <= brush_size; x++) {
			int x0 = x + tx;
			for (int y = -brush_size; y <= brush_size; y++) {
				int y0 = y + ty;
				double distance = x * x + y * y;
				if (distance >= brush_size_squared) {
					buffer.set(x, y, GetColourValue({x0, y0}));
					continue;
				}
				float max_sum_weighted = 0.0f;
				float colour_sum_weighted = 0.0f;

				for (int ox = -blend_range; ox <= blend_range; ox++) {
					for (int oy = -blend_range; oy <= blend_range; oy++) {
						olc::vi2d pos{x0 + ox, y0 + oy};

						max_sum_weighted += 1;
						colour_sum_weighted += GetColourValue(pos);
					}
				}

				float average = colour_sum_weighted / max_sum_weighted;
				// NOTE: When using for real thing, check if value needs to be remapped to avoid floating errors
				average = EaseInOutCubic(average);

				float curr_voxel_value = GetColourValue({x0, y0});
				
				float distance_normalised = distance / brush_size_squared;
				distance_normalised = std::max(distance_normalised * 2.0f - 1.0f, 0.0f);
				float new_voxel_value = curr_voxel_value * distance_normalised + average * (1 - distance_normalised);

				buffer.set(x, y, new_voxel_value);
			}
		}

		// apply the changes
		for (int x = -brush_size; x <= brush_size; x++) {
			int x0 = x + tx;
			for (int y = -brush_size; y <= brush_size; y++) {
				int y0 = y + ty;
				SetColourValue({x0, y0}, buffer.get(x, y));
			}
		}
	}*/

	void DestructTerrain_CircleFull(olc::vf2d vCell, olc::vf2d direction) {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		int32_t radius = brush_size;
		olc::vf2d size = { float(radius), float(radius) };
		olc::vf2d pos = vCell;

		pos -= direction * (float(radius) - 2.0f);

		FillCircle(pos, radius, olc::BLACK);
		
		SetDrawTarget(previous_target);
	}

	void DestructTerrain_CircleFractional(olc::vf2d vCell, olc::vf2d direction) {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		int32_t radius = brush_size;
		olc::vf2d size = { float(radius), float(radius) };
		olc::vf2d pos = vCell;

		pos -= direction * ((float)radius - 2.0f);

		for (int i = -radius; i <= radius; i++) {
			for (int j = -radius; j <= radius; j++) {
				int32_t distance = (olc::vi2d{0, 0} - olc::vi2d{ i, j }).mag();

				if (distance > radius) {
					continue;
				}

				float mapped = MapValue(float(distance), 0.0f, float(radius), 0.0f, 1.0f);

				// gaussian e^(-x^2)
				float value = GaussianCurve(mapped);
				value -= 0.2f;
				SubtractValueFromColour(pos + olc::vi2d{i, j}, value);
			}
		}
		
		SetDrawTarget(previous_target);
	}

	void DestructTerrain_GaussFractional(olc::vf2d vCell, olc::vf2d direction) {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		olc::vi2d last_raycast_hit_pos;

		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());
		float cone_angle = terraform_angle;
		float step = terraform_raycast_step;

		for (float i = -cone_angle; i <= cone_angle; i += step) {
			olc::vf2d rotated_dir = RotateVector(ray_dir, AngleToRadians(i));

			olc::vi2d intersection_pos;
			bool raycast_hit = RaycastPixel(ray_start_pos, rotated_dir, max_distance, intersection_pos);

			if (raycast_hit == false) {
				continue;
			}

			last_raycast_hit_pos = intersection_pos;

			float distance = (ray_start_pos - intersection_pos).mag();

			float mapped;
			if (i < 0) {
				mapped = MapValue(i, -cone_angle, 0, -1.0f, 0.0f);
			}
			else {
				mapped = MapValue(i, 0, cone_angle, 0.0f, 1.0f);
			}

			// gaussian e^(-x^2)
			float value = GaussianCurve(mapped);
			value -= 0.3f;

			SubtractValueFromColour(intersection_pos, value);
		}

		/*for each point on curve
			pixel = Position2D(point)
			pixel.rate = easing_function(distance(point, origin))
			dir = normalize(point - origin) * step_factor

			for i in (0, some_limit)
				pixel = Position2D(point + dir * i)
				pixel.rate = easing_function(distance(origin, point + dir * i))*/

		SetDrawTarget(previous_target);
	}

	void RestoreTerrain_GaussFractional(olc::vf2d vCell, olc::vf2d direction) {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		olc::vi2d last_raycast_hit_pos;

		olc::vf2d ray_start_pos = player_pos;
		olc::vf2d ray_dir = (mouse_pos - player_pos).norm();

		float max_distance = std::min(raycast_max_distance, (mouse_pos - player_pos).mag());
		float cone_angle = terraform_angle;
		float step = terraform_raycast_step;

		for (float i = -cone_angle; i <= cone_angle; i += step) {
			olc::vf2d rotated_dir = RotateVector(ray_dir, AngleToRadians(i));

			olc::vi2d intersection_pos;
			bool raycast_hit = RaycastPrePixel(ray_start_pos, rotated_dir, max_distance, intersection_pos);

			if (raycast_hit == false) {
				continue;
			}

			last_raycast_hit_pos = intersection_pos;

			float distance = (ray_start_pos - intersection_pos).mag();

			float mapped;
			if (i < 0) {
				mapped = MapValue(i, -cone_angle, 0, -1.0f, 0.0f);
			}
			else {
				mapped = MapValue(i, 0, cone_angle, 0.0f, 1.0f);
			}

			// gaussian e^(-x^2)
			float value = GaussianCurve(mapped);
			value -= 0.3f;

			AddValueToColour(intersection_pos, value);
		}

		/*for each point on curve
			pixel = Position2D(point)
			pixel.rate = easing_function(distance(point, origin))
			dir = normalize(point - origin) * step_factor

			for i in (0, some_limit)
				pixel = Position2D(point + dir * i)
				pixel.rate = easing_function(distance(origin, point + dir * i))*/

		SetDrawTarget(previous_target);
	}

	bool MapLocationIsEmpty(olc::vi2d pos) {
		olc::Pixel pixel = map.GetPixel(pos);
		return pixel.r == 0 && pixel.g == 0 && pixel.b == 0;
	}

	bool MapLocationIsFull(olc::vi2d pos) {
		olc::Pixel pixel = map.GetPixel(pos);
		return pixel.r == 255 && pixel.g == 255 && pixel.b == 255;
	}

	void SubtractValueFromColour(olc::vi2d pos, float fraction) {
		olc::Pixel pixel = map.GetPixel(pos);
		float curr_fraction = float(pixel.r) / 255.0f;

		float result_fraction = std::clamp(curr_fraction - fraction, 0.0f, 1.0f);
		uint8_t projected = uint8_t(result_fraction * 255.0f);
		map.SetPixel(pos, olc::Pixel{ projected, projected, projected });
	}

	void AddValueToColour(olc::vi2d pos, float fraction) {
		olc::Pixel pixel = map.GetPixel(pos);
		float curr_fraction = float(pixel.r) / 255.0f;

		float result_fraction = std::clamp(curr_fraction + fraction, 0.0f, 1.0f);
		uint8_t projected = uint8_t(result_fraction * 255.0f);
		map.SetPixel(pos, olc::Pixel{ projected, projected, projected });
	}

	void SetColourValue(olc::vi2d pos, float fraction) {
		olc::Pixel pixel = map.GetPixel(pos);

		float result_fraction = std::clamp(fraction, 0.0f, 1.0f);
		uint8_t projected = uint8_t(result_fraction * 255.0f);
		map.SetPixel(pos, olc::Pixel{ projected, projected, projected });
	}

	float GetColourValue(olc::vi2d pos) {
		olc::Pixel pixel = map.GetPixel(pos);
		float fraction = float(pixel.r) / 255.0f;
		return fraction;
	}

	void ResetMap() {
		olc::Sprite* previous_target = GetDrawTarget();
		SetDrawTarget(&map);

		FillRect({ 0,0 }, map.Size(), olc::BLACK);
		
		SetDrawTarget(previous_target);
	}

	olc::vf2d RotateVector(olc::vf2d vec, float radians) {
		olc::vf2d result;
		result.x = vec.x * std::cos(radians) + vec.y * std::sin(radians);
		result.y = vec.x * -std::sin(radians) + vec.y * std::cos(radians);
		return result;
	}

	float AngleToRadians(float angle) {
		return angle * float(M_PI) / 180.0f;
	}

	float MapValue(float value, float in_min, float in_max, float out_min, float out_max) {
		return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
	}

	float GaussianCurve(float x) {
		return float(pow(M_E, -x * x));
	}

	float EaseInOutSine(float x) {
		return -(cos(M_PI * x) - 1.0) / 2.0;
	}

	float EaseInOutCubic(float x) {
		return (x < 0.5) ? (4 * x * x * x) : (1 - pow(-2 * x + 2, 3) / 2);
	}

	float Lerp(float from, float to, float t) {
		return from * (1 - t) + to * t;
	}
};

int main()
{
	Example demo;
	if (demo.Construct(512, 512, 1, 1, false, true, false))
		demo.Start();
	return 0;
}