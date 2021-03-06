<?php
// $Id: messaging_method_mail.class.inc,v 1.1.2.1 2010/06/04 17:57:11 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Send_Method class file
 */

/**
 * Simple message instance
 */
class Messaging_Simple extends Messaging_Object {
  public $mgid;
  public $msid;
  public $uid;
  public $sender;
  public $subject;
  public $body;
  public $language;
  // Timestamps
  public $sent;
  public $timeread = 0;
  /**
   * Build from db object
   */
  public static function build_object($template) {
    return new Messaging_Simple($template);
  }
  /**
   * Build from user account and Messaging_Message
   */
  public static function build($user, $message) {
    $store = new Messaging_Simple();
    $store->uid = $user->uid;
    $store->msid = $message->msid;
    $store->sender = $message->get_sender('uid');
    $store->language = $message->get_language()->language;
    $store->subject = $message->get_subject();
    $store->body = $message->get_body();
    return $store;
  }
  /**
   * Save to db
   */
  public function save() {
    $this->sent = time();
    return drupal_write_record('messaging_simple', $this);
  }
  /**
   * Mark when read
   */
  public function read() {
    $this->timeread = time();
    db_update('messaging_simple')
      ->fields(array('timeread' => $this->timeread))
      ->condition('mgid', $this->mgid)
      ->execute();
    return $this->timeread;
  }
  /**
   * Load from db
   */
  public static function load($mgid) {
    if ($message = db_query("SELECT * FROM {messaging_simple} WHERE mgid = :mgid", array(':mgid' => $mgid))->fetchObject()) {
      return self::build_object($message); 
    }
  }
}

/**
 * Web sending method
 */
class Messaging_Simple_Method extends Messaging_Send_Method {
  // Default group and address type
  public $method = 'simple';
  public $type = 'web';
  public $anonymous = FALSE;
  public $format = MESSAGING_FORMAT_HTML;

  /**
   * Get address type
   */
  public static function address_type() {
    return 'user';
  }
  /**
   * It doesn't support anonymous destinations
   */
  function supports_anonymous() {
    return FALSE;
  }
  /**
   * Send message to address, use sending callback
   */
  function send_address($user, $message) {
    return $this->send_user($user, $message); 
  }
  /**
   * Send to user. Just store the message to be shown later.
   */
  function send_user($user, $message) {
    $user = messaging_user_object($user);
    $simple = Messaging_Simple::build($user, $message);
    return (bool)$simple->save();
  }
}
