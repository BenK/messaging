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

/**
 * Base class for all Incoming and Sending methods
 */
abstract class Messaging_Method {
  // Method identifier
  public $method;
  // Method type
  public $type;
  // Method group
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
    foreach ($info as $key => $value) {
      $this->$key = $value;
    }
    $this->info = $info;
  }
  /**
   * Get method title for administrator list
   */
  function get_title() {
    return $this->method_info('title', t('Method'));
  }
  /**
   * Get method name, may be overridden by info properties
   */
  function get_name() {
    return $this->method_info('name', t('Messaging'));
  }
  /**
   * Get method description, may be overridden by info properties
   */
  function get_description() {
    return $this->method_info('description', t('Send messages.'));
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
   // Default parameters for send method
   public $method = 'send';
   public $type = 'web';
   public $enabled = TRUE;
   // Available for anonymous users
   public $anonymous = FALSE;
   // Qeueue messages instead of sending
   public $queue = FALSE;

   public static function address_type() {
     return 'user';
   }
   /**
   * Message processing: Decide on queue, log, cron and send options, prepare parameters
   * 
   * At this stage, the message can be still redirected through other sending method, or marked for discard
   */
  function message_prepare($message) {
    if (empty($message->priority) && $this->queue) {
      $message->queue = 1;
    }
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
   */  
  function message_render($message) {
    $message->subject = drupal_render($message->text['subject']);
    $message->body = drupal_render($message->text['body']);
    $message->rendered = TRUE;
  }
  
  /**
   * Send message to a single or multiple destination
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
   * Get destination for user account
   * 
   * @param $account
   *   User account object or uid
   */
  function user_destination($account) {
    return Messaging_Destination::build_user($this->address, $account);
  }

  /**
   * Check user access to this method
   */
  function user_access($account) {
    if (!$account->uid && !$this->anonymous) {
      return FALSE;
    }
    if ($permission = $this->method_info('access')) {
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
    if ($this->address == 'user') {
      return (int)$address;
    }
  }
  /**
   * Validate address
   */
  public static function address_validate($address) {
    return Messaging_Destination::static_invoke(self::address_type(), 'validate_address', $address);
  }

  /**
   * Format address for display
   */
  public static function format_address($address, $format = MESSAGING_FORMAT_PLAIN) {
    return Messaging_Destination::static_invoke(self::address_type(), 'format_address', $address, $format);
  }

  /**
   * Prepare message for specific user. Check availability, redirect, etc..
   * 
   * Redirecting is only possible when we are sending to a user account, not for anonymous destinations
   */
  function message_user($message) {

  }

  /**
   * Check whether it supports anonyous destination
   */
  function supports_anonymous() {
    return TRUE;
  }
  /**
   * Invoke static method on method type.
   * 
   * @param $type
   *   Address type
   * @param $method
   *   Method name
   * @param $arg1, $arg2...
   */
  public static function static_invoke() {
    $args = func_get_args();
    $method = array_shift($args);
    $function = array_shift($args);
    $class = messaging_address_info($method, 'class', 'Messaging_Send_Method');
    return call_user_func_array(array($class, $function), $args);
  } 
}