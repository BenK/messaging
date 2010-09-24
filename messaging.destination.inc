<?php
// $Id: messaging_destination.class.inc,v 1.1.2.13 2010/04/29 18:48:42 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Default class file
 */

/**
 * Message destination class
 */
class Messaging_Destination extends Messaging_Object {
  // Destination status
  const STATUS_PENDING = 0;
  const STATUS_ACTIVE = 1;

  // Object unique id
  public $mdid = 0;
  // Destination type
  public $type;
  // User id.
  public $uid;
  // Address for this sending method
  public $address;
  // Date sent
  public $sent = 0;
  // Status
  public $status = 0;

  /**
   * Get name for display
   */
  function get_name() {
    return $this->address_info('name', $this->address_name());
  }
  /**
   * Get default address name
   */
  function address_name() {
    return t('Destination');
  }

  /**
   * Get address for sending
   */
  function get_address() {
    return $this->address;
  }
  /**
   * Set user for this destination
   */
  function set_user($user) {
    $this->uid = $user->uid;
    if (!isset($this->address)) {
      $this->address = $this->user2address($user);
    }
    return $this;
  }

  /**
   * Get from db using conditions
   */
  public static function get($params) {
    $dest = self::load_multiple(array(), $params);
    return reset($dest);
  }

  /**
   * Build destination from db object
   */
  public static function build_object($template) {
    $class = messaging_address_type($template->type, 'class', 'Messaging_Destination');
    return new $class($template);  
  }
  /**
   * Build from address
   */
  public static function build_address($type, $address) {
    $class = messaging_address_type($type, 'class', 'Messaging_Destination');
    $destination = new $class();
    $destination->type = $type;
    $destination->address = $address;
    return $destination;
  }
  /**
   * Load object from database
   */
  public static function load($mdid) {
    $dest = entity_load('messaging_destination', array($mdid));
    return $dest ? $dest[$mdid] : FALSE;
  }
  /**
   * Load multiple events
   */
  public static function load_multiple($mdids = array(), $conditions = array()) {
    return entity_load('messaging_destination', $mdids, $conditions);
  }

  /**
   * Validate values to create a destination
   * 
   * @param $method
   *   Send method
   * @param $address
   *   Address value
   * @param $account
   *   Optional user id or account object the destination is intended for
   */
  public static function validate_method($method, $address, $account = NULL) {
    // First validate address and check permission
    $send_method = messaging_send_method($method);
    if (!$send_method || !$send_method->address_validate($address)) {
      return FALSE;
    }
    if (isset($account)) {
      $account = messaging_user_object($account);
      if (!$account || !$send_method->user_access($account)) {
        return FALSE;
      }
    }
    if ($type = messaging_method_info($method, 'address_type')) {
      return self::validate_type($type, $address, $account);
    }
  }
  /**
   * Validate values to create a destination
   * 
   * @param $type
   *   Address type
   * @param $address
   *   Address value
   * @param $account
   *   Optional user id or account object
   */
  public static function validate_type($type, $address, $account = NULL) {
    // First try validate callback for this address type
    if (!self::validate_address($type, $address)) {
      return FALSE;
    }
    elseif (isset($account)) {
      $uid = messaging_user_uid($account);
      if ($existing = self::get_by_address($type, $address)) {
        // There's already a destination with these parameters, check user
        // It will be ok if users match or it was anonymous before
        return !isset($account) || !$existing->uid || $existing->uid == $uid;
      }
      elseif ($address_uid = self::address2uid($type, $address)) {
        // Ok if this address belongs to the same user or to no user 
        return !$address_uid || $address_uid == $uid;
      }
    }
    return TRUE;
  }

  /**
   * Validate address format
   */
  public static function validate_address($address) {
    return !empty($address);
  }
  /**
   * Map address to uid
   */
  public static function address2uid($type, $address) {
    return 0;
  }
  
  /**
   * Create from array data
   */
  public static function create($data) {
    // If no more clues, we create it for anonymous user
    $data += array('uid' => 0, 'method' => NULL, 'type' => NULL, 'address' => NULL);
    if ($data['type'] && $data['address']) {
      return self::create_type($data['type'], $data['address'], $data['uid']);
    }
    else {
      return self::create_method($data['method'], $data['address'], $data['uid']);
    }
  }

  /**
   * Create for sending method
   */
  public static function create_method($send_method, $address, $uid) {
    if ($type = messaging_method_info($send_method, 'address_type')) {
      return self::create_type($type, $address, $uid);
    }
  }
  /**
   * Create with parameters
   */
  public static function create_type($type, $address, $uid) {
    if ($existing = self::get_by_address($type, $address)) {
      if ($existing->uid != $uid) {
        $existing->uid = $uid;    
        $existing->save();
      }
      return $existing;
    }
    // Transitional case, row for user with no address, update it
    elseif ($uid && ($existing = self::get(array('uid' => $uid, 'type' => $type, 'address' => '')))) {
      $existing->address = $address;
      $existing->save();
      return $existing;
    }
    else {
      $destination = self::build(array('type' => $type, 'address' => $address, 'uid' => $uid));
      $destination->save();
      return $destination;
    }
  }
  /**
   * Get destination by method and address. This allows advanced caching.
   */
  public static function get_by_address($type, $address) {
    $cached = self::cache_by_address($type, $address);
    if (isset($cached)) {
      return $cached;
    }
    else {
      return self::get(array('type' => $type, 'address' => $address));
    }
  }

  /**
   * Get unique index for this destination
   */
  function index() {
    return $this->uid . ':' . $this->type . ':' . $this->address;
  }

  /**
   * Get address type information
   */
  function address_info($property = NULL, $default = NULL) {
    if (!empty($this->type)) {
      return messaging_address_info($this->type, $property, $default);
    }
  }

  /**
   * Format address
   */
  function format_address($html = FALSE) {
    return check_plain($this->address);
  }
  /**
   * Get user account
   */
  function get_user() {
    return isset($this->uid) ? messaging_user_object($this->uid) : NULL;
  }
  /**
   * Delete from db and cache
   */
  
  /**
   * Delete messaging destination object/s
   */
  public static function delete_multiple($params) {
    return self::db_query("DELETE", $params);
  }
  // Magic function, format as string
  public function __toString() {
    return 'Destination: ' . $this->index();
  }

  /**
   * Save object to cache
   */
  public function cache_save() {
    $this->cache_by_address($this->type, $this->address, $this);
  }
  /**
   * Save object to cache
   */
  public function cache_delete() {
    parent::cache_delete();
    $this->cache_by_address($this->type, $this->address, FALSE);
  }
  // Store into address cache
  public static function cache_set($key, $object) {
    $cache = &drupal_static('messaging_destination_address');
    return $cache[$key] = $object;
  }
  // Get from address cache
  public static function cache_get($key) {
    $cache = &drupal_static('messaging_destination_address');
    return isset($cache[$key]) ? $cache[$key] : NULL;
  }
  /**
   * Cache get/set by method and address
   */
  public static function cache_by_address($type, $address, $object = NULL) {
    if (isset($object)) {
      return self::cache_set("$type:$address", $object);
    }
    else {
      return self::cache_get("$type:$address");
    }
  }
  /**
   * Get user from address
   */
  static function address2user($address) {
    return NULL;
  }
  /**
   * Get address from user
   */
  static function user2address($user) {
    return NULL;
  }

}

/**
 * Destination is a system user
 */
class Messaging_User_Destination extends Messaging_Destination {
  public $address = '';
  /**
   * Get name for display
   */
  function address_name() { 
    return t('User');
  }
  /**
   * Format address
   */
  function format_address($html = FALSE) {
    if ($user = $this->get_user()) {
      return $html ? theme('username', array('account' => $user)) : check_plain($user->name);
    }
  }
  /**
   * Get address for sending
   */
  function get_address() {
    return $this->get_user();
  }
  /**
   * Set user for this destination
   */
  function set_user($user) {
    $this->uid = $user->uid;
  }
  /**
   * Get user from address
   */
  static function address2user($address) {
    return $address;
  }
  /**
   * Get address from user
   */
  static function user2address($user) {
    return $user;
  }
  /**
   * Check address is valid
   */
  static function validate_address($address) {
    return is_object($address) ? !empty($address->uid);
  }
}