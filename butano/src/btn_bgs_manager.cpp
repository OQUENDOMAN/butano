#include "btn_bgs_manager.h"

#include "btn_bgs.h"
#include "btn_pool.h"
#include "btn_color.h"
#include "btn_vector.h"
#include "btn_camera.h"
#include "btn_display.h"
#include "btn_algorithm.h"
#include "btn_bg_tiles_ptr.h"
#include "btn_display_manager.h"
#include "btn_regular_bg_attributes.h"
#include "../hw/include/btn_hw_bgs.h"

namespace btn::bgs_manager
{

namespace
{
    class item_type
    {

    public:
        fixed_point position;
        size quarter_dimensions;
        unsigned usages = 1;
        sort_key bg_sort_key;
        hw::bgs::handle handle;
        regular_bg_map_ptr map;
        uint8_t handles_index = -1;
        unsigned blending_enabled: 1;
        unsigned visible: 1;
        unsigned ignore_camera: 1;
        unsigned update: 1;

        item_type(const regular_bg_builder& builder, regular_bg_map_ptr&& _map) :
            position(builder.position()),
            bg_sort_key(builder.priority(), builder.z_order()),
            map(move(_map)),
            blending_enabled(builder.blending_enabled()),
            visible(builder.visible()),
            ignore_camera(builder.ignore_camera()),
            update(true)
        {
            hw::bgs::setup_regular(builder, handle);
            hw::bgs::set_tiles_cbb(map.tiles().cbb(), handle);
            update_map();
        }

        void update_map()
        {
            size map_dimensions = map.dimensions();
            hw::bgs::set_map_sbb(map.id(), handle);
            hw::bgs::set_bpp_mode(map.bpp_mode(), handle);
            hw::bgs::set_map_dimensions(map_dimensions, handle);
            quarter_dimensions = map_dimensions * 2;
            update_hw_position();
        }

        fixed_point hw_position() const
        {
            fixed_point result = -position;

            if(! ignore_camera)
            {
                result += camera::position();
            }

            result.set_x(result.x() + quarter_dimensions.width() + 8);
            result.set_y(result.y() + quarter_dimensions.height() + 8);
            return result;
        }

        fixed_point hw_position(const fixed_point& camera_position) const
        {
            fixed_point result = -position;

            if(! ignore_camera)
            {
                result += camera_position;
            }

            result.set_x(result.x() + quarter_dimensions.width() + 8);
            result.set_y(result.y() + quarter_dimensions.height() + 8);
            return result;
        }

        void update_hw_position()
        {
            fixed_point real_position = hw_position();
            hw::bgs::set_position(real_position.x().integer(), real_position.y().integer(), handle);
        }

        void update_hw_position(const fixed_point& camera_position)
        {
            fixed_point real_position = hw_position(camera_position);
            hw::bgs::set_position(real_position.x().integer(), real_position.y().integer(), handle);
        }
    };


    class static_data
    {

    public:
        pool<item_type, hw::bgs::count()> items_pool;
        vector<item_type*, hw::bgs::count()> items_vector;
        hw::bgs::handle handles[hw::bgs::count()];
        bool rebuild_handles = false;
        bool commit = false;
    };

    BTN_DATA_EWRAM static_data data;


    void _insert_item(item_type& new_item)
    {
        sort_key bg_sort_key = new_item.bg_sort_key;

        if(new_item.visible)
        {
            data.rebuild_handles = true;
        }

        for(auto it = data.items_vector.begin(), end = data.items_vector.end(); it != end; ++it)
        {
            const item_type* item = *it;

            if(bg_sort_key < item->bg_sort_key)
            {
                data.items_vector.insert(it, &new_item);
                return;
            }
        }

        data.items_vector.push_back(&new_item);
    }

    void _update_item(const item_type& item)
    {
        if(! data.rebuild_handles && item.visible)
        {
            data.handles[item.handles_index] = item.handle;
            data.commit = true;
        }
    }

    pair<int, int> _indexes(id_type id, id_type other_id)
    {
        auto item = static_cast<const item_type*>(id);
        auto other_item = static_cast<const item_type*>(other_id);
        int index = -1;
        int other_index = -1;

        for(int current_index = 0, limit = data.items_vector.size(); current_index < limit; ++current_index)
        {
            const item_type* current_item = data.items_vector[current_index];

            if(current_item == item)
            {
                index = current_index;
            }
            else if(current_item == other_item)
            {
                other_index = current_index;
            }
        }

        return make_pair(index, other_index);
    }

    void _set_regular_attributes(const regular_bg_map_ptr& current_map, const regular_bg_attributes& attributes,
                                 uint16_t& bg_cnt)
    {
        const regular_bg_map_ptr& attributes_map = attributes.map();
        BTN_ASSERT(current_map.dimensions() == attributes_map.dimensions(), "Map dimensions mismatch");

        hw::bgs::set_tiles_cbb(attributes_map.tiles().cbb(), bg_cnt);
        hw::bgs::set_map_sbb(attributes_map.id(), bg_cnt);
        hw::bgs::set_bpp_mode(attributes_map.bpp_mode(), bg_cnt);
        hw::bgs::set_priority(attributes.priority(), bg_cnt);
        hw::bgs::set_mosaic_enabled(attributes.mosaic_enabled(), bg_cnt);
    }
}

int used_count()
{
    return data.items_vector.size();
}

int available_count()
{
    return data.items_vector.available();
}

id_type create(regular_bg_builder&& builder)
{
    BTN_ASSERT(! data.items_vector.full(), "No more available bgs");

    item_type& item = data.items_pool.create(builder, builder.release_map());
    _insert_item(item);
    display_manager::set_show_bg_in_all_windows(&item, true);
    return &item;
}

id_type optional_create(regular_bg_builder&& builder)
{
    if(data.items_vector.full())
    {
        return nullptr;
    }

    optional<regular_bg_map_ptr> map = builder.optional_release_map();

    if(! map)
    {
        return nullptr;
    }

    item_type& item = data.items_pool.create(builder, move(*map));
    _insert_item(item);
    display_manager::set_show_bg_in_all_windows(&item, true);
    return &item;
}

void increase_usages(id_type id)
{
    auto item = static_cast<item_type*>(id);
    ++item->usages;
}

void decrease_usages(id_type id)
{
    auto item = static_cast<item_type*>(id);
    --item->usages;

    if(! item->usages)
    {
        if(! data.rebuild_handles && item->visible)
        {
            if(! data.items_vector.empty() && item != data.items_vector.back())
            {
                data.rebuild_handles = true;
            }
            else
            {
                display_manager::set_bg_enabled(item->handles_index, false);
            }
        }

        display_manager::set_show_bg_in_all_windows(&item, false);
        erase(data.items_vector, item);
        data.items_pool.destroy(*item);
    }
}

optional<int> hw_id(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    int handles_index = item->handles_index;
    optional<int> result;

    if(handles_index >= 0)
    {
        result = handles_index;
    }

    return result;
}

size dimensions(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->quarter_dimensions * 4;
}

const regular_bg_map_ptr& map(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->map;
}

void set_map(id_type id, const regular_bg_map_ptr& map)
{
    auto item = static_cast<item_type*>(id);

    if(map != item->map)
    {
        item->map = map;
        item->update_map();
        _update_item(*item);
    }
}

void set_map(id_type id, regular_bg_map_ptr&& map)
{
    auto item = static_cast<item_type*>(id);

    if(map != item->map)
    {
        item->map = move(map);
        item->update_map();
        _update_item(*item);
    }
}

const fixed_point& position(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->position;
}

fixed_point hw_position(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->hw_position();
}

void set_position(id_type id, const fixed_point& position)
{
    auto item = static_cast<item_type*>(id);
    item->position = position;
    item->update_hw_position();
    _update_item(*item);
}

int priority(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->bg_sort_key.priority();
}

void set_priority(id_type id, int priority)
{
    BTN_ASSERT(priority >= 0 && priority <= bgs::max_priority(), "Invalid priority: ", priority);

    auto item = static_cast<item_type*>(id);

    if(item->bg_sort_key.priority() != priority)
    {
        item->bg_sort_key.set_priority(priority);
        hw::bgs::set_priority(priority, item->handle);
        erase(data.items_vector, item);
        _insert_item(*item);
    }
}

int z_order(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->bg_sort_key.z_order();
}

void set_z_order(id_type id, int z_order)
{
    auto item = static_cast<item_type*>(id);

    if(item->bg_sort_key.z_order() != z_order)
    {
        item->bg_sort_key.set_z_order(z_order);
        erase(data.items_vector, item);
        _insert_item(*item);
    }
}

bool above(id_type id, id_type other_id)
{
    pair<int, int> indexes = _indexes(id, other_id);
    return indexes.first > indexes.second;
}

void swap_order(id_type id, id_type other_id)
{
    auto item = static_cast<item_type*>(id);
    auto other_item = static_cast<item_type*>(other_id);

    if(item->visible || other_item->visible)
    {
        data.rebuild_handles = true;
    }

    pair<int, int> indexes = _indexes(id, other_id);
    swap(data.items_vector[indexes.first], data.items_vector[indexes.second]);
}

bool mosaic_enabled(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return hw::bgs::mosaic_enabled(item->handle);
}

void set_mosaic_enabled(id_type id, bool mosaic_enabled)
{
    auto item = static_cast<item_type*>(id);
    hw::bgs::set_mosaic_enabled(mosaic_enabled, item->handle);
    _update_item(*item);
}

regular_bg_attributes regular_attributes(id_type id)
{
    return regular_bg_attributes(map(id), priority(id), mosaic_enabled(id));
}

void set_regular_attributes(id_type id, const regular_bg_attributes& attributes)
{
    auto item = static_cast<item_type*>(id);
    set_priority(id, attributes.priority());
    _set_regular_attributes(item->map, attributes, item->handle.cnt);
    item->map = attributes.map();
    _update_item(*item);
}

bool blending_enabled(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->blending_enabled;
}

void set_blending_enabled(id_type id, bool blending_enabled)
{
    auto item = static_cast<item_type*>(id);
    item->blending_enabled = blending_enabled;

    if(! data.rebuild_handles && item->visible)
    {
        display_manager::set_blending_bg_enabled(item->handles_index, blending_enabled);
    }
}

bool visible(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->visible;
}

void set_visible(id_type id, bool visible)
{
    auto item = static_cast<item_type*>(id);

    if(visible != static_cast<bool>(item->visible))
    {
        item->visible = visible;
        data.rebuild_handles = true;
    }
}

bool ignore_camera(id_type id)
{
    auto item = static_cast<const item_type*>(id);
    return item->ignore_camera;
}

void set_ignore_camera(id_type id, bool ignore_camera)
{
    auto item = static_cast<item_type*>(id);
    item->ignore_camera = ignore_camera;
    item->update_hw_position();
    _update_item(*item);
}

void update_map_tiles_cbb(int map_id, int tiles_cbb)
{
    for(item_type* item : data.items_vector)
    {
        if(item->map.id() == map_id)
        {
            hw::bgs::set_tiles_cbb(tiles_cbb, item->handle);
            _update_item(*item);
        }
    }
}

void update_map_palette_bpp_mode(int map_id, palette_bpp_mode new_bpp_mode)
{
    for(item_type* item : data.items_vector)
    {
        if(item->map.id() == map_id)
        {
            hw::bgs::set_bpp_mode(new_bpp_mode, item->handle);
            _update_item(*item);
        }
    }
}

void update_camera()
{
    fixed_point camera_position = camera::position();

    for(item_type* item : data.items_vector)
    {
        if(! item->ignore_camera)
        {
            item->update_hw_position(camera_position);
            _update_item(*item);
        }
    }
}

void fill_hblank_effect_horizontal_positions(fixed base_position, const fixed* positions_ptr, uint16_t* dest_ptr)
{
    if(base_position == 0)
    {
        for(int index = 0, limit = display::height(); index < limit; ++index)
        {
            dest_ptr[index] = uint16_t(positions_ptr[index].integer());
        }
    }
    else
    {
        for(int index = 0, limit = display::height(); index < limit; ++index)
        {
            dest_ptr[index] = uint16_t((base_position + positions_ptr[index]).integer());
        }
    }
}

void fill_hblank_effect_vertical_positions(fixed base_position, const fixed* positions_ptr, uint16_t* dest_ptr)
{
    if(base_position == 0)
    {
        for(int index = 0, limit = display::height(); index < limit; ++index)
        {
            dest_ptr[index] = uint16_t(positions_ptr[index].integer());
        }
    }
    else
    {
        for(int index = 0, limit = display::height(); index < limit; ++index)
        {
            dest_ptr[index] = uint16_t((base_position + positions_ptr[index]).integer());
        }
    }
}

void fill_hblank_effect_regular_attributes(id_type id, const regular_bg_attributes* attributes_ptr, uint16_t* dest_ptr)
{
    auto item = static_cast<item_type*>(id);
    const regular_bg_map_ptr& map = item->map;
    uint16_t bg_cnt = item->handle.cnt;

    for(int index = 0, limit = display::height(); index < limit; ++index)
    {
        const regular_bg_attributes& attributes = attributes_ptr[index];
        uint16_t& dest_cnt = dest_ptr[index];
        dest_cnt = bg_cnt;
        _set_regular_attributes(map, attributes, dest_cnt);
    }
}

void update()
{
    if(data.rebuild_handles)
    {
        int id = 3;
        data.rebuild_handles = false;
        data.commit = true;

        for(item_type* item : data.items_vector)
        {
            if(item->visible)
            {
                item->handles_index = id;
                data.handles[id] = item->handle;
                display_manager::set_bg_enabled(id, true);
                display_manager::set_blending_bg_enabled(id, item->blending_enabled);
                --id;
            }
            else
            {
                item->handles_index = -1;
            }
        }

        for(int index = 0; index <= id; ++index)
        {
            display_manager::set_bg_enabled(index, false);
        }
    }
}

void commit()
{
    if(data.commit)
    {
        hw::bgs::commit(data.handles);
        data.commit = false;
    }
}

}
