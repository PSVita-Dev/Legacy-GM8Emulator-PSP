#include "Game.hpp"
#include "GameRenderer.hpp"
#include "Instance.hpp"
#include "CodeActionManager.hpp"
#include "CodeRunner.hpp"


bool Game::LoadRoom(unsigned int id) {
	// Exit if we're already in this room
	if (id == _globals.room) return true;

	// Get the Room object for the room we're going to
	Room* room = _assetManager.GetRoom(id);

	// Check room exists
	if (!room->exists) return false;

	for (unsigned int i = 0; i < _instances.Count(); i++) {
		// run "room end" event for _instances[i]
		Object* o = _assetManager.GetObject(_instances[i]->object_index);
		for (IndexedEvent e : o->evOther) {
			if (e.index == 5) {
				if (!_codeActions->Run(e.actions, e.actionCount, _instances[i]->id, NULL)) return false;
				break;
			}
		}
	}

	// Delete non-persistent instances
	_instances.ClearNonPersistent();

	// Update renderer
	_renderer->ResizeGameWindow(room->width, room->height);
	_renderer->SetGameWindowTitle(room->caption);
	_renderer->SetBGColour(room->backgroundColour);

	// Update room
	_globals.room = id;
	if (room->speed != _lastUsedRoomSpeed) {
		_globals.room_speed = room->speed;
		_lastUsedRoomSpeed = _globals.room_speed;
		_globals.room_width = room->width;
		_globals.room_height = room->height;
	}

	// Create all instances in new room
	for (unsigned int i = 0; i < room->instanceCount; i++) {
		if (!_instances.GetInstanceByNumber(room->instances[i].id)) {
			unsigned int id = room->instances[i].id;
			Instance* instance = _instances.AddInstance(id, room->instances[i].x, room->instances[i].y, room->instances[i].objectIndex);
			if (!instance) {
				// Failed to create instance
				return false;
			}
			// run room->instances[i] creation code
			if (!_runner->Run(room->instances[i].creation, id, NULL)) return false;
			// run instance create event
			Object* o = _assetManager.GetObject(instance->object_index);
			if (!_codeActions->Run(o->evCreate, o->evCreateActionCount, instance->id, NULL)) return false;
		}
	}

	// run room's creation code
	if (!_runner->Run(room->creationCode, NULL, NULL)) return false;

	for (unsigned int i = 0; i < _instances.Count(); i++) {
		// run _instances[i] room start event
		Object* o = _assetManager.GetObject(_instances[i]->object_index);
		for (IndexedEvent e : o->evOther) {
			if (e.index == 4) {
				if (!_codeActions->Run(e.actions, e.actionCount, _instances[i]->id, NULL)) return false;
				break;
			}
		}
	}

	return true;
}



bool Game::Frame() {
	// Run draw event for all instances (TODO: correct depth order)
	for (unsigned int i = 0; i < _instances.Count(); i++) {
		Instance* instance = _instances[i];
		// Don't run draw event for instances that don't exist or aren't visible.
		if (instance->exists && instance->visible) {

			Object* obj = _assetManager.GetObject(instance->object_index);
			if (obj->evDraw) {
				// This object has a custom draw event.
				if (!_codeActions->Run(obj->evDraw, obj->evDrawActionCount, instance->id, NULL)) return false;
			}
			else {
				// This is the default draw action if no draw event is present for this object (more or less.)
				// We can just do this for now until we have code compilation working.
				if (instance->sprite_index >= 0) {
					Sprite* sprite = _assetManager.GetSprite(instance->sprite_index);
					if (sprite->exists) {
						_renderer->DrawImage(sprite->frames[((int)instance->image_index) % sprite->frameCount], instance->x, instance->y, instance->image_xscale, instance->image_yscale, instance->image_angle, instance->image_blend, instance->image_alpha);
					}
					else {
						// Tried to draw non-existent sprite
						return false;
					}
				}
			}
		}
	}

	// Draw screen
	_renderer->RenderFrame();
	if (_renderer->ShouldClose()) return false;

	// TODO: "begin step" trigger events

	// Run "begin step" event for all instances
	for (unsigned int i = 0; i < _instances.Count(); i++) {
		Instance* instance = _instances[i];
		if (instance->exists) {
			Object* o = _assetManager.GetObject(instance->object_index);
			if (!_codeActions->Run(o->evStepBegin, o->evStepBeginActionCount, instance->id, NULL)) return false;
		}
	}

	// TODO: if timeline_running, add timeline_speed to timeline_position and then run any events in that timeline indexed BELOW (not equal to) the current timeline_position

	// TODO: subtract from alarms and run event if they reach 0

	// TODO: keyboard events
	
	// TODO: mouse events

	// TODO: key press events
	
	// TODO: key release events

	// TODO: "normal step" trigger events

	// Run "step" event for all instances
	for (unsigned int i = 0; i < _instances.Count(); i++) {
		Instance* instance = _instances[i];
		if (instance->exists) {
			Object* o = _assetManager.GetObject(instance->object_index);
			if (!_codeActions->Run(o->evStep, o->evStepActionCount, instance->id, NULL)) return false;
		}
	}

	// Movement
	for (unsigned int i = 0; i < _instances.Count(); i++) {
		Instance* instance = _instances[i];
		if (instance->exists) {
			// Set xprevious and yprevious
			instance->xprevious = instance->x;
			instance->yprevious = instance->y;

			if (instance->friction != 0) {
				// Subtract friction from speed towards 0
				bool neg = instance->speed < 0;
				instance->speed = abs(instance->speed) - instance->friction;
				if (instance->speed < 0) instance->speed = 0;
				else if (neg) instance->speed = -instance->speed;

				// Recalculate hspeed/vspeed
				instance->hspeed = cos(instance->direction * PI / 180.0) * instance->speed;
				instance->vspeed = -sin(instance->direction * PI / 180.0) * instance->speed;
			}

			if (instance->gravity) {
				// Apply gravity in gravity_direction to hspeed and vspeed
				instance->hspeed += cos(instance->gravity_direction * PI / 180.0) * instance->gravity;
				instance->vspeed += -sin(instance->gravity_direction * PI / 180.0) * instance->gravity;

				// Recalculate speed and direction from hspeed/vspeed
				instance->direction = atan(-instance->vspeed / instance->hspeed) * 180.0 / PI;
				instance->speed = sqrt(pow(instance->hspeed, 2) + pow(instance->vspeed, 2));
			}

			// Apply hspeed and vspeed to x and y
			instance->x += instance->hspeed;
			instance->y += instance->vspeed;
		}
	}

	// TODO: in this order: "outside room" events for all instances, "intersect boundary" events for all instances
	// TODO: in this order, if views are enabled: "outside view x" events for all instances, "intersect boundary view x" events for all instances

	// TODO: collision events for all instances

	// TODO: "end step" trigger events

	// Run "end step" event for all instances
	for (unsigned int i = 0; i < _instances.Count(); i++) {
		Instance* instance = _instances[i];
		if (instance->exists) {
			Object* o = _assetManager.GetObject(instance->object_index);
			if (!_codeActions->Run(o->evStepEnd, o->evStepEndActionCount, instance->id, NULL)) return false;
		}
	}

	_instances.ClearDeleted();

	return true;
}