<?php
// $Id: messaging_method.class.inc,v 1.1.2.16 2010/05/24 16:06:35 jareyero Exp $
/**
 * @file
 * Drupal Messaging Framework - Send_Method class file
 */

/**
 * Base class for all Incoming and Sending methods
 */
abstract class Messaging_Method {
  public $method;
  public $type;
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
  }
  /**
   * Get info property
   */
  function get_info($property = NULL) {
    if ($property) {
      return isset($this->info[$property]) ? $this->info[$property] : NULL;
    }
    else {
      return $this->info;
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
      $message->text['body'] += array('#theme' => 'message_body')
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
    return $this->send_address($message->get_address(), $message);
  }
  
  /**
   * Message default callback: send iterating over all destinations
   * 
   * This can be overridden by any sending method that can send multiple messages in a more efficient way.
   */
  function message_send_multiple($message, $addresses) {
    $success = $fail = array();
    foreach ($addresses as $to) {
      if ($this->send_address($to, $message)) {
        $success[] = $to;
      }
      else {
        $fail[] = $to;
      }
    }
    // If sent, set time. If failed set error
    if ($success) {
      $message->sent = time();
    }
    if ($fail) {
      $destinations = check_plain(implode(', ', $fail));
      $message->set_error("Sending ($this->method) to some destinations failed: $destinations.");
    }
    return $success && !$fail;
  }
  
  /**
   * Queue message for next delivery
   * 
   * By default it is saved to the store, though some sending methods like 'simple' may not consider queueing.
   * 
   * This will depend pretty much on the sending method as for some of them queueing is more expensive than sending.
   */
  function message_queue($message) {
    $message->queued = TRUE;
    messaging_queue()->createItem($message);
    $queue = DrupalQueue::get('messaging_queue');
    return $queue->createItem($message);
  }
  
  /**
   * Queue for multiple addresses
   */
  function message_queue_multiple($message, $addresses) {
    
  }
}