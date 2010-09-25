<?php
// $Id: messaging_method.class.inc,v 1.1.2.16 2010/05/24 16:06:35 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Send_Method class file
 */

// Outgoing method
define('MESSAGING_TYPE_OUTGOING', 4);
// Incoming method
define('MESSAGING_TYPE_INCOMING', 8);
// Method with push delivery. Messages will be pushed to the user using messaging sending methods.
define('MESSAGING_TYPE_PUSH', 1);
// Method type with pull delivery. Messages will be pulled from queue by the plug-in
define('MESSAGING_TYPE_PULL', 2);

// Disable queue for this method, when sending is more 'expensive' than queueing (like for simple method)
define('MESSAGING_TYPE_NOQUEUE', 16);
// Shorthand type: Push + Outgoing
define('MESSAGING_TYPE_SEND', MESSAGING_TYPE_PUSH | MESSAGING_TYPE_OUTGOING);

/**
 * Base class for all Incoming and Sending methods
 */
abstract class Messaging_Method {
  const TYPE_OUTGOING = MESSAGING_TYPE_OUTGOING;
  const TYPE_INCOMING = MESSAGING_TYPE_INCOMING;
  
  public $method;
  public $type;
  public $title;
  public $name;
  public $description;
  public $enabled = TRUE;
  public $anonymous = FALSE;
  // Remaining info array
  public $info = array();
  /**
   * Build send method from info array
   *
   * Some of the array values will be set as properties for the object. Some others won't as they're just
   * for formatting, so they'll be kept only in the $object->info array
   */
  function __construct($method, $info = array()) {
    $this->method = $method;
    $this->info = $info;
    foreach ($info as $key => $value) {
      $this->$key = $value;
    }
  }

  function name() {
    return $this->method_info('name', $this->method_name());
  }
  function description() {
    return $this->method_info('description', $this->method_description());
  }
  function method_name() {
    return t('Messaging method');
  }
  function method_description() {
    return  t('Generic messaging method.');
  }
  /**
   * Get info property
   */
  function method_info($property = NULL, $default = NULL) {
    if ($property) {
      return isset($this->info[$property]) ? $this->info[$property] : $default;
    }
    else {
      return $this->info;
    }
  }
  /**
   * Returns default messaging method
   */
  static function default_method($account = NULL) {
    if ($account && !empty($account->messaging_default) && messaging_method($account->messaging_default)->user_access($account)) {
      return $account->messaging_default;
    }
    elseif ($method = variable_get('messaging_default_method', '')) {
      return $method;
    }
    else {
      return key(messaging_method_info());
    }
  }

  /**
   * Update messaging method.
   * 
   * When a messaging method is disabled, we need to update current settings for this and other modues
   * 
   * @param $method
   *   Method to disable
   * @param $replace
   *   Optional replacement method suggested by the disabled one.
   */
  static function method_disable($method, $replace = NULL) {
    module_load_include('install', 'messaging');
    $replace = isset($replace) ? $replace : messaging_update_method_replace($method, TRUE);
    messaging_update_method_disable($method, $replace);
    if ($replace) {
      drupal_set_message(t('Disabled messaging sending method %method and replaced by %replace', array('%method' => messaging_method_info($method, 'title'), '%replace' => messaging_method_info($replace, 'title'))));
    } else {
      // It seems all methods are disabled, print warning
      drupal_set_message(t('Disabled messaging sending method but cannot find a replacement. Please, enable some other sending method.'), 'error');
    }  
  }
  
}

/**
 * Sending method, implements all specific method functionality
 * 
 * Old callback functions are
 * - send
 * - destination
 */
class Messaging_Send_Method extends Messaging_Method {
  // Method with push delivery. Messages will be pushed to the user using messaging sending methods.
  const TYPE_PUSH = MESSAGING_TYPE_PUSH;
  // Method type with pull delivery. Messages will be pulled using messaging pull methods
  const TYPE_PULL = MESSAGING_TYPE_PULL;
  // Disable queueing (sending is more expensive than queueing)
  const TYPE_NOQUEUE = MESSAGING_TYPE_NOQUEUE;
  // Shorthand type: Push + Outgoing
  const TYPE_SEND = MESSAGING_TYPE_SEND;

   /**
   * Message processing: Decide on queue, log, cron and send options, prepare parameters
   * 
   * At this stage, the message can be still redirected through other sending method, or marked for discard
   */
  function message_prepare($message) {
    if (is_array($message->text['subject'])) {
      $message->text['subject'] += array('#theme' => 'message_subject');
    }
    if (is_array($text['body'])) {
      $message->text['body'] += array('#theme' => 'message_body');
    }
    $message->prepared = TRUE;
  }

  /**
   * Renders full message with header and body
   * 
   * @param $message
   *   Message object
   */  
  function message_render($message) {
    $message->subject = drupal_render($message->text['subject']);
    $message->body = drupal_render($message->text['body']);
    $message->rendered = TRUE;
  }
  
  /**
   * Send message to a single destination
   * 
   * @param $message
   *   Message object
   */
  function message_send($message) {
    $results = array();
    foreach ($message->get_destinations() as $key => $destination) {
      $results[$key] = $this->send_destination($destination, $message);
    }
    $message->add_results($results);
    return (boolean)array_filter($results);
  }
  
  /**
   * Send message to destination
   */
  function send_destination($destination, $message) {
    return $this->send_address($destination->get_address(), $message);
  }
  /**
   * Send message to address
   */
  function send_address($address, $message) {
    return FALSE;
  }
  
  /**
   * Queue message for next delivery
   * 
   * By default it is saved to the store, though some sending methods like 'simple' may not consider queueing.
   * 
   * If no queue available we send the message
   */
  function message_queue($message) {
    if ($queue = messaging_store('queue')) {
      return $queue->message_queue($message);
    }
    else {
      $message->queued = FALSE;
      return $this->message_send($message);
    }
  }
  
  /**
   * Queue for multiple addresses
   */
  function message_queue_multiple($message, $addresses) {
    
  }
  /**
   * Get destination for user account
   * 
   * @param $account
   *   User account object or uid
   */
  function user_destination($account) {
    return Messaging_Destination::build_user($this->address_type, $account);
  }

  /**
   * Check user access to this method
   */
  function user_access($account) {
    if (!$account->uid && !$this->anonymous) {
      return FALSE;
    }
    if ($permission = $this->get_info('access')) {
      return user_access($permission, $account);
    }
    else {
      return TRUE;
    }
  }

  /**
   * Get address name
   */
  function address_name() {
    if (empty($this->address_name)) {
      if ($name = $this->get_address_info('name')) {
        $this->address_name = $name;
      }
      else {
        $this->address_name = t('Address');
      }
    } 
    return $this->address_name; 
  }

  /**
   * Get uid for address
   */
  function get_address_uid($address) {
    if ($this->address_type == 'user') {
      return (int)$address;
    }
    elseif ($function = $this->get_address_info('address2uid callback')) {
      return $function($address);
    }
  }
  /**
   * Validate address
   */
  function address_validate($address) {
    if ($function = $this->get_address_info('validate callback')) {
      return $function($address);
    }
    else {
      // The default address will be valid if not empty
      return !empty($address);
    }
  }
  /**
   * Format address for display
   */
  function format_address($address, $html = FALSE) {
    if ($function = $this->get_address_info('format callback')) {
      return $function($address, $html);
    }
    else {
      return check_plain($address);
    }
  }

  /**
   * Prepare message for specific user. Check availability, redirect, etc..
   * 
   * Redirecting is only possible when we are sending to a user account, not for anonymous destinations
   */
  function message_user($message, $account = NULL) {
    if ($callback = $this->get_info('user callback')) {
      $account = $account ? $account : $message->get_user();
      call_user_func($callback, $message, $account);
    }
  }

  /**
   * Check whether it supports anonyous destination
   */
  function supports_anonymous() {
    return TRUE;
  }
}