#include "best_stoptime.h"

namespace navitia { namespace routing {

std::pair<const type::StopTime*, uint32_t>
best_stop_time(const type::JourneyPatternPoint* jpp,
               const DateTime dt,
               const type::AccessibiliteParams & accessibilite_params,
               const bool clockwise, const type::Data &data, bool reconstructing_path) {
    if(clockwise)
        return earliest_stop_time(jpp, dt, data, reconstructing_path, accessibilite_params);
    else
        return tardiest_stop_time(jpp, dt, data, reconstructing_path, accessibilite_params);
}



/** Which is the first valid stop_time in this range ?
 *  Returns invalid_idx is none is
 */
const type::StopTime* valid_pick_up(type::idx_t idx, type::idx_t end, uint32_t date,
              uint32_t hour, const type::Data &data, bool reconstructing_path,
              const type::VehicleProperties &required_vehicle_properties){
    for(; idx < end; ++idx) {
        const type::StopTime* st = data.dataRaptor.st_idx_forward[idx];
        if (st->departure_validity_pattern->check(date)) {
            if( st->valid_end(reconstructing_path) && st->valid_hour(hour, true)
                    && st->vehicle_journey->accessible(required_vehicle_properties) ){
                return st;
            }
        }
    }
    return nullptr;
}

const type::StopTime* valid_drop_off(type::idx_t idx, type::idx_t end, uint32_t date,
               uint32_t hour, const type::Data &data, bool reconstructing_path,
               const type::VehicleProperties &required_vehicle_properties){
    for(; idx < end; ++idx) {
        const type::StopTime* st = data.dataRaptor.st_idx_backward[idx];
        if (st->arrival_validity_pattern->check(date)) {
            if( st->valid_end(!reconstructing_path) && st->valid_hour(hour, false)
                    && st->vehicle_journey->accessible(required_vehicle_properties) ){
                return st;
            }
        }
    }
    return nullptr;
}

std::pair<const type::StopTime*, uint32_t>
earliest_stop_time(const type::JourneyPatternPoint* jpp,
                   const DateTime dt, const type::Data &data,
                   bool reconstructing_path,
                   const type::AccessibiliteParams & accessibilite_params) {

    // If the stop_point doesn’t match the required properties,
    // we don’t bother looking further
    if(!jpp->stop_point->accessible(accessibilite_params.properties))
        return std::make_pair(nullptr, 0);


    //On cherche le plus petit stop time de la journey_pattern >= dt.hour()
    auto begin = data.dataRaptor.departure_times.begin() +
            data.dataRaptor.first_stop_time[jpp->journey_pattern->idx] +
            jpp->order * data.dataRaptor.nb_trips[jpp->journey_pattern->idx];
    auto end = begin + data.dataRaptor.nb_trips[jpp->journey_pattern->idx];
    const auto bound_predicate = [](uint32_t departure_time, uint32_t hour){
                               return departure_time < hour;};
    auto it = std::lower_bound(begin, end, DateTimeUtils::hour(dt),
                               bound_predicate);

    type::idx_t idx = it - data.dataRaptor.departure_times.begin();
    type::idx_t end_idx = (begin - data.dataRaptor.departure_times.begin()) +
                           data.dataRaptor.nb_trips[jpp->journey_pattern->idx];

    //On renvoie le premier trip valide
    const type::StopTime* first_st = valid_pick_up(idx, end_idx,
            DateTimeUtils::date(dt), DateTimeUtils::hour(dt), data,
            reconstructing_path, accessibilite_params.vehicle_properties);
    auto working_dt = dt;
    // If no trip was found, we look for one the day after
    if(first_st == nullptr) {
        idx = begin - data.dataRaptor.departure_times.begin();
        working_dt = DateTimeUtils::set(DateTimeUtils::date(dt)+1, 0);
        first_st = valid_pick_up(idx, end_idx, DateTimeUtils::date(working_dt), 0,
            data, reconstructing_path, accessibilite_params.vehicle_properties);
    }

    if(first_st != nullptr) {
        if(!first_st->is_frequency()) {
            DateTimeUtils::update(working_dt, first_st->departure_time);
        } else {
            working_dt = dt;
            const DateTime tmp_dt = f_departure_time(DateTimeUtils::hour(working_dt), first_st);
            DateTimeUtils::update(working_dt, DateTimeUtils::hour(tmp_dt));
        }
        return std::make_pair(first_st, working_dt);
    }

    //Cette journey_pattern ne comporte aucun trip compatible
    return std::make_pair(nullptr, 0);
}


std::pair<const type::StopTime*, uint32_t>
tardiest_stop_time(const type::JourneyPatternPoint* jpp,
                   const DateTime dt, const type::Data &data,
                   bool reconstructing_path,
                   const type::AccessibiliteParams & accessibilite_params) {
    if(!jpp->stop_point->accessible(accessibilite_params.properties))
        return std::make_pair(nullptr, 0);
    //On cherche le plus grand stop time de la journey_pattern <= dt.hour()
    const auto begin = data.dataRaptor.arrival_times.begin() +
                       data.dataRaptor.first_stop_time[jpp->journey_pattern->idx] +
                       jpp->order * data.dataRaptor.nb_trips[jpp->journey_pattern->idx];
    const auto end = begin + data.dataRaptor.nb_trips[jpp->journey_pattern->idx];
    const auto bound_predicate = [](uint32_t arrival_time, uint32_t hour){
                                  return arrival_time > hour;};
    auto it = std::lower_bound(begin, end, DateTimeUtils::hour(dt), bound_predicate);

    type::idx_t idx = it - data.dataRaptor.arrival_times.begin();
    type::idx_t end_idx = (begin - data.dataRaptor.arrival_times.begin()) +
                           data.dataRaptor.nb_trips[jpp->journey_pattern->idx];

    const type::StopTime* first_st = valid_drop_off(idx, end_idx,
            DateTimeUtils::date(dt), DateTimeUtils::hour(dt), data,
            reconstructing_path, accessibilite_params.vehicle_properties);

    auto working_dt = dt;
    // If no trip was found, we look for one the day before
    if(first_st == nullptr && DateTimeUtils::date(dt) > 0){
        idx = begin - data.dataRaptor.arrival_times.begin();
        working_dt = DateTimeUtils::set(DateTimeUtils::date(working_dt) - 1,
                                        DateTimeUtils::SECONDS_PER_DAY - 1);
        first_st = valid_drop_off(idx, end_idx, DateTimeUtils::date(working_dt),
                DateTimeUtils::SECONDS_PER_DAY - 1, data, reconstructing_path,
                accessibilite_params.vehicle_properties);
    }

    if(first_st != nullptr){
        if(!first_st->is_frequency()) {
            DateTimeUtils::update(working_dt, DateTimeUtils::hour(first_st->arrival_time), false);
        } else {
            working_dt = dt;
            const DateTime tmp_dt = f_arrival_time(DateTimeUtils::hour(working_dt), first_st);
            DateTimeUtils::update(working_dt, DateTimeUtils::hour(tmp_dt), false);
        }
        return std::make_pair(first_st, working_dt);
    }

    //Cette journey_pattern ne comporte aucun trip compatible
    return std::make_pair(nullptr, 0);
}
}}

